/**
 * Tests for Meta_Parse() — .META JSON parser.
 *
 * Tests cover:
 *   - Valid META with size and crop
 *   - Whitespace / formatting variations
 *   - Missing fields
 *   - Corrupt input
 *   - Zero-length input
 *   - Large values
 *
 * Build: g++ -std=c++17 -o test_meta_parser test_meta_parser.cpp ../meta_parser.cpp
 */

#include "test_framework.h"
#include "../meta_parser.h"

#include <cstring>

/* ─── Tests ─────────────────────────────────────────────────── */

TEST(meta_parse_valid) {
    const char json[] = R"({"size":[384,384],"crop":[10,20,364,354]})";
    SpriteMeta meta = {};
    EXPECT_TRUE(Meta_Parse(json, strlen(json), meta));
    EXPECT_EQ(meta.canvas_width, 384);
    EXPECT_EQ(meta.canvas_height, 384);
    EXPECT_EQ(meta.crop_x, 10);
    EXPECT_EQ(meta.crop_y, 20);
    EXPECT_EQ(meta.crop_width, 364);
    EXPECT_EQ(meta.crop_height, 354);
    PASS();
}

TEST(meta_parse_with_spaces) {
    const char json[] = R"( { "size" : [ 256 , 256 ] , "crop" : [ 0 , 0 , 256 , 250 ] } )";
    SpriteMeta meta = {};
    EXPECT_TRUE(Meta_Parse(json, strlen(json), meta));
    EXPECT_EQ(meta.canvas_width, 256);
    EXPECT_EQ(meta.canvas_height, 256);
    EXPECT_EQ(meta.crop_x, 0);
    EXPECT_EQ(meta.crop_y, 0);
    EXPECT_EQ(meta.crop_width, 256);
    EXPECT_EQ(meta.crop_height, 250);
    PASS();
}

TEST(meta_parse_with_newlines) {
    const char json[] = "{\n  \"size\": [120, 120],\n  \"crop\": [5, 3, 110, 115]\n}\n";
    SpriteMeta meta = {};
    EXPECT_TRUE(Meta_Parse(json, strlen(json), meta));
    EXPECT_EQ(meta.canvas_width, 120);
    EXPECT_EQ(meta.canvas_height, 120);
    EXPECT_EQ(meta.crop_x, 5);
    EXPECT_EQ(meta.crop_y, 3);
    EXPECT_EQ(meta.crop_width, 110);
    EXPECT_EQ(meta.crop_height, 115);
    PASS();
}

TEST(meta_parse_crop_before_size) {
    /* Fields in different order — parser should handle both */
    const char json[] = R"({"crop":[1,2,3,4],"size":[100,200]})";
    SpriteMeta meta = {};
    EXPECT_TRUE(Meta_Parse(json, strlen(json), meta));
    EXPECT_EQ(meta.canvas_width, 100);
    EXPECT_EQ(meta.canvas_height, 200);
    EXPECT_EQ(meta.crop_x, 1);
    EXPECT_EQ(meta.crop_y, 2);
    EXPECT_EQ(meta.crop_width, 3);
    EXPECT_EQ(meta.crop_height, 4);
    PASS();
}

TEST(meta_parse_missing_size) {
    const char json[] = R"({"crop":[0,0,64,64]})";
    SpriteMeta meta = {};
    EXPECT_FALSE(Meta_Parse(json, strlen(json), meta));
    PASS();
}

TEST(meta_parse_missing_crop) {
    const char json[] = R"({"size":[64,64]})";
    SpriteMeta meta = {};
    EXPECT_FALSE(Meta_Parse(json, strlen(json), meta));
    PASS();
}

TEST(meta_parse_empty_input) {
    SpriteMeta meta = {};
    EXPECT_FALSE(Meta_Parse("", 0, meta));
    PASS();
}

TEST(meta_parse_null_input) {
    SpriteMeta meta = {};
    EXPECT_FALSE(Meta_Parse(nullptr, 0, meta));
    PASS();
}

TEST(meta_parse_garbage) {
    const char data[] = "not json at all!!!";
    SpriteMeta meta = {};
    EXPECT_FALSE(Meta_Parse(data, strlen(data), meta));
    PASS();
}

TEST(meta_parse_incomplete_array) {
    const char json[] = R"({"size":[384],"crop":[0,0,384,384]})";
    SpriteMeta meta = {};
    EXPECT_FALSE(Meta_Parse(json, strlen(json), meta));
    PASS();
}

TEST(meta_parse_large_values) {
    const char json[] = R"({"size":[4096,4096],"crop":[0,0,4096,4096]})";
    SpriteMeta meta = {};
    EXPECT_TRUE(Meta_Parse(json, strlen(json), meta));
    EXPECT_EQ(meta.canvas_width, 4096);
    EXPECT_EQ(meta.canvas_height, 4096);
    PASS();
}

TEST(meta_parse_zero_crop) {
    const char json[] = R"({"size":[64,64],"crop":[0,0,0,0]})";
    SpriteMeta meta = {};
    EXPECT_TRUE(Meta_Parse(json, strlen(json), meta));
    EXPECT_EQ(meta.crop_width, 0);
    EXPECT_EQ(meta.crop_height, 0);
    PASS();
}

int main() {
    printf("test_meta_parser\n");
    return RUN_TESTS();
}
