#include <unity.h>
#include <string>
#include "app/library/library_index.h"

void setUp()
{
}

void tearDown()
{
}

void test_the_same_contents_get_the_same_id()
{
  const uint8_t a[] = {'M', 'T', 'h', 'd', 1, 2, 3}, b[] = {'M', 'T', 'h', 'd', 1, 2, 4};
  std::string id = pieceId(a, sizeof(a));
  TEST_ASSERT_EQUAL_UINT32(16, id.size());
  TEST_ASSERT_EQUAL_STRING(id.c_str(), pieceId(a, sizeof(a)).c_str());
  TEST_ASSERT_TRUE(id != pieceId(b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("cbf29ce484222325", pieceId(a, 0).c_str()); // FNV-1a's start
}

void test_names_are_cleaned()
{
  TEST_ASSERT_EQUAL_STRING("Nocturne", cleanPieceName("  Nocturne \t ").c_str());
  TEST_ASSERT_EQUAL_STRING("untitled", cleanPieceName("   ").c_str());
  TEST_ASSERT_EQUAL_STRING("untitled", cleanPieceName("").c_str());
  std::string longName(100, 'a');
  TEST_ASSERT_EQUAL_UINT32(MAX_PIECE_NAME, cleanPieceName(longName).size());
  // Cut at 80 bytes without splitting "é" (2 bytes) across it.
  std::string accents = std::string(79, 'a') + "\xC3\xA9" + "bc";
  TEST_ASSERT_EQUAL_STRING(std::string(79, 'a').c_str(), cleanPieceName(accents).c_str());
}

void test_names_from_files_drop_the_extension()
{
  TEST_ASSERT_EQUAL_STRING("Liszt - La Campanella", pieceNameFromFile("Liszt - La Campanella.mid").c_str());
  TEST_ASSERT_EQUAL_STRING("Etude", pieceNameFromFile("Etude.MIDI").c_str());
  TEST_ASSERT_EQUAL_STRING("song.mid.txt", pieceNameFromFile("song.mid.txt").c_str());
  TEST_ASSERT_EQUAL_STRING("untitled", pieceNameFromFile(".mid").c_str());
}

void test_the_index_reads_back_what_it_wrote()
{
  PieceInfo piece;
  piece.id = "0123456789abcdef";
  piece.name = "Clair de lune";
  piece.bytes = 12345;
  piece.durationMs = 300000;
  piece.notes = 2048;
  piece.added = 1790000000;
  piece.lastPlayed = 1790001000;
  piece.dynamics.volume = 80;
  piece.dynamics.contrast = 130;
  piece.dynamics.voicing = 150;
  piece.dynamics.repeatBoost = 12;
  piece.dynamics.speed = 85;
  piece.dynamics.mutedParts = 2;
  piece.parts = {0, -6, 4};
  std::string json = libraryIndexJson({piece});
  std::vector<PieceInfo> read;
  TEST_ASSERT_TRUE(readLibraryIndex(json.data(), json.size(), read));
  TEST_ASSERT_EQUAL_UINT32(1, read.size());
  TEST_ASSERT_EQUAL_STRING(json.c_str(), libraryIndexJson(read).c_str());
}

void test_unusable_entries_are_skipped()
{
  std::string json = "{\"pieces\":[{\"name\":\"no id\"},{\"id\":\"short\"},{\"id\":\"0123456789abcdef\"}]}";
  std::vector<PieceInfo> read;
  TEST_ASSERT_TRUE(readLibraryIndex(json.data(), json.size(), read));
  TEST_ASSERT_EQUAL_UINT32(1, read.size());
  TEST_ASSERT_EQUAL_STRING("untitled", read[0].name.c_str());
  TEST_ASSERT_EQUAL_UINT16(100, read[0].dynamics.speed);
  TEST_ASSERT_EQUAL_UINT16(100, read[0].dynamics.volume);
  TEST_ASSERT_FALSE(readLibraryIndex("[]", 2, read));
  TEST_ASSERT_EQUAL_UINT32(1, read.size()); // unchanged
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_the_same_contents_get_the_same_id);
  RUN_TEST(test_names_are_cleaned);
  RUN_TEST(test_names_from_files_drop_the_extension);
  RUN_TEST(test_the_index_reads_back_what_it_wrote);
  RUN_TEST(test_unusable_entries_are_skipped);
  return UNITY_END();
}
