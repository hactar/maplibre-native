#pragma once

// THIS IS A GENERATED FILE; EDIT offline_schema.sql INSTEAD
// To regenerate, run `node platform/default/include/mbgl/storage/offline_schema.js`

namespace mbgl {

static constexpr const char* offlineDatabaseSchema =
    "CREATE TABLE resources (\n"
    "  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,\n"
    "  url TEXT NOT NULL,\n"
    "  kind INTEGER NOT NULL,\n"
    "  expires INTEGER,\n"
    "  modified INTEGER,\n"
    "  etag TEXT,\n"
    "  data BLOB,\n"
    "  compressed INTEGER NOT NULL DEFAULT 0,\n"
    "  accessed INTEGER NOT NULL,\n"
    "  must_revalidate INTEGER NOT NULL DEFAULT 0,\n"
    "  UNIQUE (url)\n"
    ");\n"
    "CREATE TABLE tiles (\n"
    "  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,\n"
    "  url_template TEXT NOT NULL,\n"
    "  pixel_ratio INTEGER NOT NULL,\n"
    "  z INTEGER NOT NULL,\n"
    "  x INTEGER NOT NULL,\n"
    "  y INTEGER NOT NULL,\n"
    "  expires INTEGER,\n"
    "  modified INTEGER,\n"
    "  etag TEXT,\n"
    "  data BLOB,\n"
    "  compressed INTEGER NOT NULL DEFAULT 0,\n"
    "  accessed INTEGER NOT NULL,\n"
    "  must_revalidate INTEGER NOT NULL DEFAULT 0,\n"
    "  UNIQUE (url_template, pixel_ratio, z, x, y)\n"
    ");\n"
    "CREATE TABLE regions (\n"
    "  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,\n"
    "  definition TEXT NOT NULL,\n"
    "  description BLOB\n"
    ");\n"
    "CREATE TABLE region_resources (\n"
    "  region_id INTEGER NOT NULL REFERENCES regions(id) ON DELETE CASCADE,\n"
    "  resource_id INTEGER NOT NULL REFERENCES resources(id),\n"
    "  UNIQUE (region_id, resource_id)\n"
    ");\n"
    "CREATE TABLE region_tiles (\n"
    "  region_id INTEGER NOT NULL REFERENCES regions(id) ON DELETE CASCADE,\n"
    "  tile_id INTEGER NOT NULL REFERENCES tiles(id),\n"
    "  UNIQUE (region_id, tile_id)\n"
    ");\n"
    "CREATE INDEX resources_accessed\n"
    "ON resources (accessed);\n"
    "CREATE INDEX tiles_accessed\n"
    "ON tiles (accessed);\n"
    "CREATE INDEX region_resources_resource_id\n"
    "ON region_resources (resource_id);\n"
    "CREATE INDEX region_tiles_tile_id\n"
    "ON region_tiles (tile_id);\n";

} // namespace mbgl
