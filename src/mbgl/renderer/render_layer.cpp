#include <mbgl/renderer/render_layer.hpp>

#include <mbgl/gfx/context.hpp>
#include <mbgl/renderer/paint_parameters.hpp>
#include <mbgl/renderer/render_source.hpp>
#include <mbgl/renderer/render_tile.hpp>
#include <mbgl/style/color_ramp_property_value.hpp>
#include <mbgl/style/layer.hpp>
#include <mbgl/style/layer_properties.hpp>
#include <mbgl/style/types.hpp>
#include <mbgl/tile/tile.hpp>
#include <mbgl/util/logging.hpp>

#if MLN_DRAWABLE_RENDERER
#include <mbgl/renderer/layer_group.hpp>
#endif

namespace mbgl {

using namespace style;

RenderLayer::RenderLayer(Immutable<style::LayerProperties> properties)
    : evaluatedProperties(std::move(properties)),
      baseImpl(evaluatedProperties->baseImpl) {}

void RenderLayer::transition(const TransitionParameters& parameters, Immutable<style::Layer::Impl> newImpl) {
    baseImpl = std::move(newImpl);
    transition(parameters);
}

bool RenderLayer::needsPlacement() const {
    return baseImpl->getTypeInfo()->crossTileIndex == style::LayerTypeInfo::CrossTileIndex::Required &&
           !placementData.empty();
}

const std::string& RenderLayer::getID() const {
    return baseImpl->id;
}

int32_t RenderLayer::getLayerIndex() const noexcept {
    return layerIndex;
}

bool RenderLayer::hasRenderPass(RenderPass pass) const {
    return passes & pass;
}

bool RenderLayer::needsRendering() const {
    return passes != RenderPass::None && baseImpl->visibility != style::VisibilityType::None;
}

bool RenderLayer::supportsZoom(float zoom) const {
    // TODO: shall we use rounding or epsilon comparisons?
    return baseImpl->minZoom <= zoom && baseImpl->maxZoom >= zoom;
}

void RenderLayer::prepare(const LayerPrepareParameters& params) {
    assert(params.source);
    assert(params.source->isEnabled());
    renderTiles = params.source->getRenderTiles();
    addRenderPassesFromTiles();

#if MLN_DRAWABLE_RENDERER
    updateRenderTileIDs();
#endif // MLN_DRAWABLE_RENDERER
}

std::optional<Color> RenderLayer::getSolidBackground() const {
    return std::nullopt;
}

#if MLN_DRAWABLE_RENDERER
void RenderLayer::replaceTweaker(LayerTweakerPtr& curTweaker,
                                 LayerTweakerPtr newTweaker,
                                 const std::vector<LayerGroupBasePtr>& layerGroups) {
    const auto prevTweaker = curTweaker;

    // We need to re-create the tweaker because it doesn't yet support modifying evaluated
    // properties, but we don't want to stop updating drawables (as in the `layerChanged` case)
    // so we need to update the tweaker reference on the outstanding drawables so that they
    // pass the check in `updateExisting`.
    // TODO: Once the tweaker doesn't need to be re-created on each property evaluation, this won't be needed.
    for (const auto& group : layerGroups) {
        if (group) {
            group->addLayerTweaker(newTweaker);

            group->visitDrawables([&](gfx::Drawable& drawable) {
                if (drawable.getLayerTweaker() == prevTweaker) {
                    drawable.setLayerTweaker(newTweaker);
                }
            });
        }
    }

    curTweaker = std::move(newTweaker);
}

void RenderLayer::layerChanged(const TransitionParameters&,
                               const Immutable<style::Layer::Impl>&,
                               UniqueChangeRequestVec&) {
    // When a layer changes, the bucket won't be replaced until the new source(s) load.
    // If we remove the drawables here, they will just be re-created based on the current data.

    // Detach the layer tweaker, if any.  This keeps the existing tiles up-to-date with the
    // most recent evaluated properties, while a new one will be created along with new drawables
    // when tiles are loaded and new buckets are available.
    layerTweaker.reset();
}

void RenderLayer::layerRemoved(UniqueChangeRequestVec& changes) {
    removeAllDrawables();
    activateLayerGroup(layerGroup, false, changes);
}
#endif

void RenderLayer::markContextDestroyed() {
    // no-op
}

void RenderLayer::checkRenderability(const PaintParameters& parameters, const uint32_t activeBindingCount) {
    // Only warn once for every layer.
    if (hasRenderFailures) {
        return;
    }

    if (activeBindingCount > parameters.context.maximumVertexBindingCount) {
        Log::Error(Event::OpenGL,
                   "The layer '" + getID() +
                       "' uses more data-driven properties than the current device "
                       "supports, and will have rendering errors. To ensure "
                       "compatibility with this "
                       "device, use " +
                       std::to_string(activeBindingCount - gfx::Context::minimumRequiredVertexBindingCount) +
                       " fewer data driven properties in this layer.");
        hasRenderFailures = true;
    } else if (activeBindingCount > gfx::Context::minimumRequiredVertexBindingCount) {
        Log::Warning(Event::OpenGL,
                     "The layer '" + getID() +
                         "' uses more data-driven properties than some devices may "
                         "support. "
                         "Though it will render correctly on this device, it may have "
                         "rendering errors "
                         "on other devices. To ensure compatibility with all devices, "
                         "use " +
                         std::to_string(activeBindingCount - gfx::Context::minimumRequiredVertexBindingCount) +
                         "fewer "
                         "data-driven properties in this layer.");
        hasRenderFailures = true;
    }
}

void RenderLayer::addRenderPassesFromTiles() {
    assert(renderTiles);
    for (const RenderTile& tile : *renderTiles) {
        if (const LayerRenderData* renderData = tile.getLayerRenderData(*baseImpl)) {
            passes |= RenderPass(renderData->layerProperties->renderPasses);
        }
    }
}

const LayerRenderData* RenderLayer::getRenderDataForPass(const RenderTile& tile, RenderPass pass) const {
    if (const LayerRenderData* renderData = tile.getLayerRenderData(*baseImpl)) {
        return bool(RenderPass(renderData->layerProperties->renderPasses) & pass) ? renderData : nullptr;
    }
    return nullptr;
}

#if MLN_DRAWABLE_RENDERER
std::size_t RenderLayer::removeTile(RenderPass renderPass, const OverscaledTileID& tileID) {
    if (const auto tileGroup = static_cast<TileLayerGroup*>(layerGroup.get())) {
        const auto n = tileGroup->removeDrawables(renderPass, tileID).size();
        stats.drawablesRemoved += n;
        return n;
    }
    return 0;
}

std::size_t RenderLayer::removeAllDrawables() {
    if (layerGroup) {
        const auto count = layerGroup->getDrawableCount();
        stats.drawablesRemoved += count;
        layerGroup->clearDrawables();
        return count;
    }
    return 0;
}

void RenderLayer::updateRenderTileIDs() {
    const auto oldMap = renderTileIDs;
    renderTileIDs.clear();
    if (renderTiles) {
        renderTileIDs.reserve(renderTiles->size());
        for (const auto& tile : *renderTiles) {
            // If the tile existed previously, retain the mapped value
            const auto tileID = tile.get().getOverscaledTileID();
            const auto hit = oldMap.find(tileID);
            const auto bucketID = (hit != oldMap.end()) ? hit->second : util::SimpleIdentity::Empty;
            [[maybe_unused]] const auto result = renderTileIDs.insert(std::make_pair(tileID, bucketID));
            assert(result.second && "Unexpected duplicate TileID in renderTiles");
        }
    }
}

bool RenderLayer::hasRenderTile(const OverscaledTileID& tileID) const {
    return renderTileIDs.find(tileID) != renderTileIDs.end();
}

util::SimpleIdentity RenderLayer::getRenderTileBucketID(const OverscaledTileID& tileID) const {
    const auto hit = renderTileIDs.find(tileID);
    return (hit != renderTileIDs.end()) ? hit->second : util::SimpleIdentity::Empty;
}

bool RenderLayer::setRenderTileBucketID(const OverscaledTileID& tileID, util::SimpleIdentity bucketID) {
    const auto hit = renderTileIDs.find(tileID);
    if (hit != renderTileIDs.end() && hit->second != bucketID) {
        hit->second = bucketID;
        return true;
    }
    return false;
}

void RenderLayer::layerIndexChanged(int32_t newLayerIndex, UniqueChangeRequestVec& changes) {
    layerIndex = newLayerIndex;

    // Submit a change request to update the layer index of our tile layer group
    changeLayerIndex(layerGroup, newLayerIndex, changes);
}

void RenderLayer::changeLayerIndex(const LayerGroupBasePtr& group, int32_t newIndex, UniqueChangeRequestVec& changes) {
    if (group && group->getLayerIndex() != newIndex) {
        changes.emplace_back(std::make_unique<UpdateLayerGroupIndexRequest>(group, newIndex));
    }
}

void RenderLayer::markLayerRenderable(bool willRender, UniqueChangeRequestVec& changes) {
    isRenderable = willRender;

    // This layer is either being freshly included in the renderable set or excluded
    activateLayerGroup(layerGroup, willRender, changes);
}

void RenderLayer::setLayerGroup(LayerGroupBasePtr layerGroup_, UniqueChangeRequestVec& changes) {
    // Remove the active layer group, if any, before replacing it.
    activateLayerGroup(layerGroup, false, changes);

    layerGroup = std::move(layerGroup_);

    // Add the new layer group, if we're currently renderable.
    activateLayerGroup(layerGroup, isRenderable, changes);
}

/// (Un-)Register the layer group with the orchestrator
void RenderLayer::activateLayerGroup(const LayerGroupBasePtr& layerGroup_,
                                     bool activate,
                                     UniqueChangeRequestVec& changes) {
    if (layerGroup_) {
        if (activate) {
            // The RenderTree has determined this layer should be included in the renderable set for a frame
            changes.emplace_back(std::make_unique<AddLayerGroupRequest>(layerGroup_));
        } else {
            // The RenderTree is informing us we should not render anything
            changes.emplace_back(std::make_unique<RemoveLayerGroupRequest>(layerGroup_));
        }
    }
}
#endif

bool RenderLayer::applyColorRamp(const style::ColorRampPropertyValue& colorValue, PremultipliedImage& image) {
    if (colorValue.isUndefined()) {
        return false;
    }

    const auto length = image.bytes();

    for (uint32_t i = 0; i < length; i += 4) {
        const auto color = colorValue.evaluate(static_cast<double>(i) / length);
        image.data[i + 0] = static_cast<uint8_t>(std::floor(color.r * 255.f));
        image.data[i + 1] = static_cast<uint8_t>(std::floor(color.g * 255.f));
        image.data[i + 2] = static_cast<uint8_t>(std::floor(color.b * 255.f));
        image.data[i + 3] = static_cast<uint8_t>(std::floor(color.a * 255.f));
    }
    return true;
}

} // namespace mbgl
