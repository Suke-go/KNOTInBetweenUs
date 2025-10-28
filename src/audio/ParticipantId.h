#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace knot::audio {

enum class ParticipantId : std::uint8_t {
    Participant1 = 0,
    Participant2 = 1,
    Synthetic = 2,
    None = 255
};

std::string participantIdToString(ParticipantId id);
std::optional<ParticipantId> participantIdFromString(const std::string& value);

} // namespace knot::audio

