#include "ParticipantId.h"

#include <algorithm>
#include <cctype>

namespace knot::audio {

std::string participantIdToString(ParticipantId id) {
    switch (id) {
        case ParticipantId::Participant1:
            return "Participant1";
        case ParticipantId::Participant2:
            return "Participant2";
        case ParticipantId::Synthetic:
            return "Synthetic";
        case ParticipantId::None:
        default:
            return "None";
    }
}

std::optional<ParticipantId> participantIdFromString(const std::string& value) {
    std::string lowered(value.size(), '\0');
    std::transform(value.begin(), value.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lowered == "participant1" || lowered == "p1") {
        return ParticipantId::Participant1;
    }
    if (lowered == "participant2" || lowered == "p2") {
        return ParticipantId::Participant2;
    }
    if (lowered == "synthetic" || lowered == "syntheticheartbeat") {
        return ParticipantId::Synthetic;
    }
    if (lowered == "none" || lowered.empty()) {
        return ParticipantId::None;
    }
    return std::nullopt;
}

} // namespace knot::audio

