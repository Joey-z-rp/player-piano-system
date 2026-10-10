#ifndef BYTE_READER_H
#define BYTE_READER_H

#include <stddef.h>
#include <stdint.h>

// Bounds-checked big-endian reader. Every read fails instead of running
// past the end, so a truncated or corrupt file can't overrun the buffer.
class ByteReader
{
public:
  ByteReader(const uint8_t *data, size_t length) : data(data), length(length), pos(0)
  {
  }

  bool atEnd() const
  {
    return pos >= length;
  }

  bool peek(uint8_t &out) const
  {
    if (pos >= length)
    {
      return false;
    }
    out = data[pos];
    return true;
  }

  bool u8(uint8_t &out)
  {
    if (!peek(out))
    {
      return false;
    }
    pos++;
    return true;
  }

  bool u16(uint16_t &out)
  {
    uint8_t high, low;
    if (!u8(high) || !u8(low))
    {
      return false;
    }
    out = (uint16_t)((high << 8) | low);
    return true;
  }

  bool u32(uint32_t &out)
  {
    uint16_t high, low;
    if (!u16(high) || !u16(low))
    {
      return false;
    }
    out = ((uint32_t)high << 16) | low;
    return true;
  }

  // MIDI variable-length quantity: at most 4 bytes, 7 bits each.
  bool varLen(uint32_t &out)
  {
    out = 0;
    for (int i = 0; i < 4; i++)
    {
      uint8_t byte;
      if (!u8(byte))
      {
        return false;
      }
      out = (out << 7) | (byte & 0x7F);
      if ((byte & 0x80) == 0)
      {
        return true;
      }
    }
    return false;
  }

  bool skip(size_t count)
  {
    if (count > length - pos)
    {
      return false;
    }
    pos += count;
    return true;
  }

  bool sub(size_t count, ByteReader &out)
  {
    if (count > length - pos)
    {
      return false;
    }
    out = ByteReader(data + pos, count);
    pos += count;
    return true;
  }

private:
  const uint8_t *data;
  size_t length;
  size_t pos;
};

#endif // BYTE_READER_H
