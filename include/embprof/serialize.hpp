#ifndef EMBPROF_SERIALIZE_HPP
#define EMBPROF_SERIALIZE_HPP

/// @file serialize.hpp
/// @brief Compact binary serialization of profiling state.
///
/// Writes/reads raw bytes into a caller-provided buffer.  No dynamic
/// allocation, no format overhead beyond a tiny header.  Suitable for
/// sending over CAN, UART, or storing in NVRAM.
///
/// Format:
///   [magic: 4 bytes][version: 1 byte][payload_size: 2 bytes][payload...]
///
/// The payload is a memcpy of the profiling_point::state_t struct.
/// Endianness must match between sender and receiver (typical for same MCU).

#include "config.hpp"
#include "profiling_point.hpp"

#include <cstdint>
#include <cstring>

namespace embprof {

namespace detail {
    static constexpr uint8_t SERIALIZE_MAGIC[4] = { 'E', 'P', 'R', 'F' };
    static constexpr uint8_t SERIALIZE_VERSION  = 1;
    static constexpr uint32_t HEADER_SIZE       = 4 + 1 + 2; // magic + version + payload_size
} // namespace detail

/// Returns the total buffer size needed to serialize a profiling_point<N,C>.
template <uint32_t N, typename C>
constexpr uint32_t serialized_size() noexcept {
    return detail::HEADER_SIZE
         + static_cast<uint32_t>(sizeof(typename profiling_point<N, C>::state_t));
}

/// Serialize a profiling_point into a byte buffer.
/// @param pp      The profiling point to serialize.
/// @param buf     Output buffer (must be at least serialized_size<N,C>() bytes).
/// @param buf_len Available buffer length.
/// @return Number of bytes written, or 0 on failure (buffer too small).
template <uint32_t N, typename C>
uint32_t serialize(const profiling_point<N, C>& pp,
                   uint8_t* buf, uint32_t buf_len) noexcept
{
    using state_type = typename profiling_point<N, C>::state_t;
    const uint32_t payload_size = static_cast<uint32_t>(sizeof(state_type));
    const uint32_t total        = detail::HEADER_SIZE + payload_size;

    if (buf_len < total) return 0;

    // Header
    std::memcpy(buf, detail::SERIALIZE_MAGIC, 4);
    buf[4] = detail::SERIALIZE_VERSION;
    buf[5] = static_cast<uint8_t>(payload_size & 0xFF);
    buf[6] = static_cast<uint8_t>((payload_size >> 8) & 0xFF);

    // Payload
    state_type state = pp.snapshot();
    std::memcpy(buf + detail::HEADER_SIZE, &state, payload_size);

    return total;
}

/// Deserialize a profiling_point state from a byte buffer.
/// @param pp      The profiling point to restore into.
/// @param buf     Input buffer.
/// @param buf_len Buffer length.
/// @return true on success, false on magic/version/size mismatch.
template <uint32_t N, typename C>
bool deserialize(profiling_point<N, C>& pp,
                 const uint8_t* buf, uint32_t buf_len) noexcept
{
    using state_type = typename profiling_point<N, C>::state_t;
    const uint32_t payload_size = static_cast<uint32_t>(sizeof(state_type));
    const uint32_t total        = detail::HEADER_SIZE + payload_size;

    if (buf_len < total) return false;

    // Check magic
    if (std::memcmp(buf, detail::SERIALIZE_MAGIC, 4) != 0) return false;

    // Check version
    if (buf[4] != detail::SERIALIZE_VERSION) return false;

    // Check payload size
    uint16_t stored_size = static_cast<uint16_t>(buf[5])
                         | (static_cast<uint16_t>(buf[6]) << 8);
    if (stored_size != payload_size) return false;

    // Restore
    state_type state;
    std::memcpy(&state, buf + detail::HEADER_SIZE, payload_size);
    pp.restore(state);

    return true;
}

} // namespace embprof

#endif // EMBPROF_SERIALIZE_HPP
