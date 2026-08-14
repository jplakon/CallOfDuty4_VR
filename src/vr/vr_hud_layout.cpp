#include "vr_hud_layout.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace kisak::vr::hud
{
namespace
{

constexpr float kElementMargin = 16.0f;

// The stock SP compass ticker is centered horizontally and anchored to the
// bottom of the 640x480 HUD.  Its widest background is 124x13 at local
// -62/-34 after the menu's 0/-10 origin.  Objective icons extend the useful
// selection area to 144x40.  Keep that source-space center here so both the
// real ownerdraws and the visual editor use exactly the same group transform.
constexpr Point kCompassSourceCenter = {0.0f, -27.5f};
constexpr Size kCompassEditorSize = {144.0f, 40.0f};

// Keep beta.12's editor rectangle as the canonical target.  Users have already
// positioned that rectangle, and its lower-right inset formula is the public
// meaning of the saved controls.  V82 moves the real center/bottom-aligned
// ownerdraw group onto this target instead of making saved layouts jump.

float Clamp(const float value, const float minimum, const float maximum)
{
    return (std::clamp)(value, minimum, maximum);
}

bool IsSafeToken(const std::string& value)
{
    if (value.empty() || value.size() > 96u)
    {
        return false;
    }

    return std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character)
        {
            return
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '-' || character == '_' ||
                character == '.';
        });
}

std::map<std::string, std::string> ParseKeyValues(
    const std::string& text,
    std::string* error)
{
    std::map<std::string, std::string> values;
    if (text.empty() || text.size() > 8192u)
    {
        if (error != nullptr)
        {
            *error = "HUD editor message is empty or too large";
        }
        return {};
    }

    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0u)
        {
            if (error != nullptr)
            {
                *error = "HUD editor message contains a malformed line";
            }
            return {};
        }

        const std::string key = line.substr(0u, separator);
        const std::string value = line.substr(separator + 1u);
        if (!IsSafeToken(key) || values.count(key) != 0u)
        {
            if (error != nullptr)
            {
                *error = "HUD editor message contains an unsafe or duplicate key";
            }
            return {};
        }
        values[key] = value;
    }

    return values;
}

bool ParseFloat(
    const std::map<std::string, std::string>& values,
    const char* key,
    float* value)
{
    if (value == nullptr)
    {
        return false;
    }

    const auto found = values.find(key);
    if (found == values.end() || found->second.empty())
    {
        return false;
    }

    errno = 0;
    char* parsedEnd = nullptr;
    const float parsed = std::strtof(
        found->second.c_str(),
        &parsedEnd);
    if (errno == ERANGE || parsedEnd == nullptr ||
        parsedEnd != found->second.c_str() + found->second.size() ||
        !std::isfinite(parsed))
    {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseBool(
    const std::map<std::string, std::string>& values,
    const char* key,
    bool* value)
{
    if (value == nullptr)
    {
        return false;
    }
    const auto found = values.find(key);
    if (found == values.end())
    {
        return false;
    }
    if (found->second == "0")
    {
        *value = false;
        return true;
    }
    if (found->second == "1")
    {
        *value = true;
        return true;
    }
    return false;
}

bool ParseLayoutValues(
    const std::map<std::string, std::string>& values,
    Layout* layout)
{
    return layout != nullptr &&
        ParseFloat(values, "HUD_SAFE_X", &layout->safeX) &&
        ParseFloat(values, "HUD_SAFE_Y", &layout->safeY) &&
        ParseFloat(values, "AMMO_X", &layout->ammoOffsetX) &&
        ParseFloat(values, "AMMO_Y", &layout->ammoOffsetY) &&
        ParseFloat(values, "AMMO_SCALE", &layout->ammoScale) &&
        ParseBool(values, "COMPASS_ENABLED", &layout->compassEnabled) &&
        ParseFloat(values, "COMPASS_X", &layout->compassInsetX) &&
        ParseFloat(values, "COMPASS_Y", &layout->compassInsetY) &&
        ParseFloat(values, "COMPASS_SCALE", &layout->compassScale) &&
        ParseFloat(values, "NOTIFY_X", &layout->notificationOffsetX) &&
        ParseFloat(values, "NOTIFY_Y", &layout->notificationOffsetY) &&
        ParseFloat(values, "NOTIFY_SCALE", &layout->notificationScale) &&
        ParseFloat(values, "OBJECTIVE_X", &layout->objectiveOffsetX) &&
        ParseFloat(values, "OBJECTIVE_Y", &layout->objectiveOffsetY) &&
        ParseFloat(values, "OBJECTIVE_SCALE", &layout->objectiveScale) &&
        ParseFloat(values, "SUBTITLE_X", &layout->subtitleOffsetX) &&
        ParseFloat(values, "SUBTITLE_Y", &layout->subtitleOffsetY) &&
        ParseFloat(values, "SUBTITLE_SCALE", &layout->subtitleScale);
}

void WriteLayout(std::ostringstream& output, const Layout& layout)
{
    output << "HUD_SAFE_X=" << layout.safeX << "\r\n";
    output << "HUD_SAFE_Y=" << layout.safeY << "\r\n";
    output << "AMMO_X=" << layout.ammoOffsetX << "\r\n";
    output << "AMMO_Y=" << layout.ammoOffsetY << "\r\n";
    output << "AMMO_SCALE=" << layout.ammoScale << "\r\n";
    output << "COMPASS_ENABLED=" << (layout.compassEnabled ? 1 : 0)
           << "\r\n";
    output << "COMPASS_X=" << layout.compassInsetX << "\r\n";
    output << "COMPASS_Y=" << layout.compassInsetY << "\r\n";
    output << "COMPASS_SCALE=" << layout.compassScale << "\r\n";
    output << "NOTIFY_X=" << layout.notificationOffsetX << "\r\n";
    output << "NOTIFY_Y=" << layout.notificationOffsetY << "\r\n";
    output << "NOTIFY_SCALE=" << layout.notificationScale << "\r\n";
    output << "OBJECTIVE_X=" << layout.objectiveOffsetX << "\r\n";
    output << "OBJECTIVE_Y=" << layout.objectiveOffsetY << "\r\n";
    output << "OBJECTIVE_SCALE=" << layout.objectiveScale << "\r\n";
    output << "SUBTITLE_X=" << layout.subtitleOffsetX << "\r\n";
    output << "SUBTITLE_Y=" << layout.subtitleOffsetY << "\r\n";
    output << "SUBTITLE_SCALE=" << layout.subtitleScale << "\r\n";
}

bool LayoutIsFinite(const Layout& layout)
{
    const std::array<float, 17> numbers = {{
        layout.safeX,
        layout.safeY,
        layout.ammoOffsetX,
        layout.ammoOffsetY,
        layout.ammoScale,
        layout.compassInsetX,
        layout.compassInsetY,
        layout.compassScale,
        layout.notificationOffsetX,
        layout.notificationOffsetY,
        layout.notificationScale,
        layout.objectiveOffsetX,
        layout.objectiveOffsetY,
        layout.objectiveScale,
        layout.subtitleOffsetX,
        layout.subtitleOffsetY,
        layout.subtitleScale,
    }};
    return std::all_of(
        numbers.begin(),
        numbers.end(),
        [](const float value)
        {
            return std::isfinite(value);
        });
}

} // namespace

Layout DefaultLayout()
{
    return {};
}

void ClampLayout(Layout* const layout)
{
    if (layout == nullptr)
    {
        return;
    }
    if (!LayoutIsFinite(*layout))
    {
        *layout = DefaultLayout();
        return;
    }

    layout->safeX = Clamp(layout->safeX, 0.50f, 1.00f);
    layout->safeY = Clamp(layout->safeY, 0.50f, 1.00f);
    layout->ammoOffsetX = Clamp(layout->ammoOffsetX, -320.0f, 640.0f);
    layout->ammoOffsetY = Clamp(layout->ammoOffsetY, -240.0f, 480.0f);
    layout->ammoScale = Clamp(layout->ammoScale, kMinimumScale, kMaximumScale);
    layout->compassInsetX = Clamp(layout->compassInsetX, -80.0f, 600.0f);
    layout->compassInsetY = Clamp(layout->compassInsetY, -80.0f, 440.0f);
    layout->compassScale = Clamp(layout->compassScale, kMinimumScale, kMaximumScale);
    layout->notificationOffsetX = Clamp(layout->notificationOffsetX, -300.0f, 300.0f);
    layout->notificationOffsetY = Clamp(layout->notificationOffsetY, -240.0f, 400.0f);
    layout->notificationScale = Clamp(layout->notificationScale, kMinimumScale, kMaximumScale);
    layout->objectiveOffsetX = Clamp(layout->objectiveOffsetX, -300.0f, 300.0f);
    layout->objectiveOffsetY = Clamp(layout->objectiveOffsetY, -180.0f, 270.0f);
    layout->objectiveScale = Clamp(layout->objectiveScale, kMinimumScale, kMaximumScale);
    layout->subtitleOffsetX = Clamp(layout->subtitleOffsetX, -300.0f, 300.0f);
    layout->subtitleOffsetY = Clamp(layout->subtitleOffsetY, -400.0f, 80.0f);
    layout->subtitleScale = Clamp(layout->subtitleScale, kMinimumScale, kMaximumScale);
}

const char* ElementId(const Element element)
{
    switch (element)
    {
    case Element::AmmoEquipment: return "ammo";
    case Element::Compass: return "compass";
    case Element::Notifications: return "notifications";
    case Element::ObjectiveBanner: return "objective";
    case Element::Subtitles: return "subtitles";
    default: return "ammo";
    }
}

const char* ElementLabel(const Element element)
{
    switch (element)
    {
    case Element::AmmoEquipment: return "Ammo & equipment";
    case Element::Compass: return "Compass & objective icons";
    case Element::Notifications: return "Notifications";
    case Element::ObjectiveBanner: return "Objective / status banner";
    case Element::Subtitles: return "Subtitles";
    default: return "Ammo & equipment";
    }
}

bool ParseElement(const std::string& value, Element* const element)
{
    if (element == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0u; index < kElementCount; ++index)
    {
        const Element candidate = static_cast<Element>(index);
        if (value == ElementId(candidate))
        {
            *element = candidate;
            return true;
        }
    }
    return false;
}

Element CycleElement(const Element element, const int direction)
{
    const std::size_t current =
        (std::min)(
            static_cast<std::size_t>(element),
            kElementCount - 1u);
    if (direction < 0)
    {
        return static_cast<Element>(
            (current + kElementCount - 1u) % kElementCount);
    }
    if (direction > 0)
    {
        return static_cast<Element>((current + 1u) % kElementCount);
    }
    return static_cast<Element>(current);
}

Point SafeAreaMinimum(const Layout& layout)
{
    return {
        (1.0f - layout.safeX) * 0.5f * kCanvasWidth,
        (1.0f - layout.safeY) * 0.5f * kCanvasHeight,
    };
}

Point SafeAreaMaximum(const Layout& layout)
{
    const Point minimum = SafeAreaMinimum(layout);
    return {
        kCanvasWidth - minimum.x,
        kCanvasHeight - minimum.y,
    };
}

Point ElementCenter(const Layout& layout, const Element element)
{
    const Point safeMinimum = SafeAreaMinimum(layout);
    const Point safeMaximum = SafeAreaMaximum(layout);
    switch (element)
    {
    case Element::AmmoEquipment:
        return {
            safeMinimum.x + 70.0f * layout.ammoScale +
                layout.ammoOffsetX,
            safeMaximum.y - 38.0f * layout.ammoScale -
                layout.ammoOffsetY,
        };
    case Element::Compass:
        return {
            kCanvasWidth - 72.0f * layout.compassScale -
                layout.compassInsetX,
            kCanvasHeight - 72.0f * layout.compassScale -
                layout.compassInsetY,
        };
    case Element::Notifications:
        return {
            safeMinimum.x + 6.0f +
                135.0f * layout.notificationScale +
                layout.notificationOffsetX,
            10.0f + layout.notificationOffsetY +
                27.0f * layout.notificationScale,
        };
    case Element::ObjectiveBanner:
        return {
            320.0f + layout.objectiveOffsetX,
            190.0f + layout.objectiveOffsetY,
        };
    case Element::Subtitles:
        return {
            320.0f + layout.subtitleOffsetX,
            420.0f + layout.subtitleOffsetY,
        };
    default:
        return {320.0f, 240.0f};
    }
}

Size ElementSize(const Layout& layout, const Element element)
{
    const float scale = ElementScale(layout, element);
    switch (element)
    {
    case Element::AmmoEquipment: return {180.0f * scale, 76.0f * scale};
    case Element::Compass:
        return {
            kCompassEditorSize.width * scale,
            kCompassEditorSize.height * scale,
        };
    case Element::Notifications: return {270.0f * scale, 54.0f * scale};
    case Element::ObjectiveBanner: return {350.0f * scale, 68.0f * scale};
    case Element::Subtitles: return {500.0f * scale, 72.0f * scale};
    default: return {};
    }
}

Rect TransformCompassRect(
    const Layout& layout,
    const Rect sourceRect)
{
    const Point center = ElementCenter(layout, Element::Compass);
    const float scale = layout.compassScale;
    const Point targetLocalCenter = {
        center.x - kCanvasWidth * 0.5f,
        center.y - kCanvasHeight,
    };
    return {
        targetLocalCenter.x +
            (sourceRect.x - kCompassSourceCenter.x) * scale,
        targetLocalCenter.y +
            (sourceRect.y - kCompassSourceCenter.y) * scale,
        sourceRect.width * scale,
        sourceRect.height * scale,
    };
}

float ElementScale(const Layout& layout, const Element element)
{
    switch (element)
    {
    case Element::AmmoEquipment: return layout.ammoScale;
    case Element::Compass: return layout.compassScale;
    case Element::Notifications: return layout.notificationScale;
    case Element::ObjectiveBanner: return layout.objectiveScale;
    case Element::Subtitles: return layout.subtitleScale;
    default: return 1.0f;
    }
}

void SetElementScale(
    Layout* const layout,
    const Element element,
    const float scale)
{
    if (layout == nullptr)
    {
        return;
    }
    const Point originalCenter = ElementCenter(*layout, element);
    const float clamped = Clamp(scale, kMinimumScale, kMaximumScale);
    switch (element)
    {
    case Element::AmmoEquipment: layout->ammoScale = clamped; break;
    case Element::Compass: layout->compassScale = clamped; break;
    case Element::Notifications: layout->notificationScale = clamped; break;
    case Element::ObjectiveBanner: layout->objectiveScale = clamped; break;
    case Element::Subtitles: layout->subtitleScale = clamped; break;
    default: break;
    }
    MoveElement(layout, element, originalCenter, false);
}

void CenterElement(Layout* const layout, const Element element)
{
    MoveElement(
        layout,
        element,
        {kCanvasWidth * 0.5f, kCanvasHeight * 0.5f},
        false);
}

void ResetElement(Layout* const layout, const Element element)
{
    if (layout == nullptr)
    {
        return;
    }

    const Layout defaults = DefaultLayout();
    const Point defaultCenter = ElementCenter(defaults, element);
    SetElementScale(
        layout,
        element,
        ElementScale(defaults, element));
    MoveElement(layout, element, defaultCenter, false);
    if (element == Element::Compass)
    {
        layout->compassEnabled = defaults.compassEnabled;
    }
}

Point SnapPointToGuides(
    const Layout& layout,
    Point point,
    const float distance)
{
    const Point safeMinimum = SafeAreaMinimum(layout);
    const Point safeMaximum = SafeAreaMaximum(layout);
    const std::array<float, 7> horizontalGuides = {{
        kElementMargin,
        safeMinimum.x,
        64.0f,
        320.0f,
        576.0f,
        safeMaximum.x,
        kCanvasWidth - kElementMargin,
    }};
    const std::array<float, 7> verticalGuides = {{
        kElementMargin,
        safeMinimum.y,
        48.0f,
        240.0f,
        432.0f,
        safeMaximum.y,
        kCanvasHeight - kElementMargin,
    }};

    const auto snap = [distance](
        const float coordinate,
        const auto& guides)
    {
        float result = coordinate;
        float bestDistance = distance;
        for (const float guide : guides)
        {
            const float candidateDistance =
                std::abs(coordinate - guide);
            if (candidateDistance <= bestDistance)
            {
                result = guide;
                bestDistance = candidateDistance;
            }
        }
        return result;
    };

    point.x = snap(point.x, horizontalGuides);
    point.y = snap(point.y, verticalGuides);
    return point;
}

void MoveElement(
    Layout* const layout,
    const Element element,
    Point center,
    const bool snapToGuides)
{
    if (layout == nullptr)
    {
        return;
    }
    if (snapToGuides)
    {
        center = SnapPointToGuides(*layout, center);
    }
    center.x = Clamp(center.x, kElementMargin, kCanvasWidth - kElementMargin);
    center.y = Clamp(center.y, kElementMargin, kCanvasHeight - kElementMargin);

    const Point safeMinimum = SafeAreaMinimum(*layout);
    const Point safeMaximum = SafeAreaMaximum(*layout);
    switch (element)
    {
    case Element::AmmoEquipment:
        layout->ammoOffsetX = center.x -
            (safeMinimum.x + 70.0f * layout->ammoScale);
        layout->ammoOffsetY = safeMaximum.y -
            38.0f * layout->ammoScale - center.y;
        break;
    case Element::Compass:
        layout->compassInsetX = kCanvasWidth -
            72.0f * layout->compassScale - center.x;
        layout->compassInsetY = kCanvasHeight -
            72.0f * layout->compassScale - center.y;
        break;
    case Element::Notifications:
        layout->notificationOffsetX = center.x -
            (safeMinimum.x + 6.0f +
             135.0f * layout->notificationScale);
        layout->notificationOffsetY = center.y -
            (10.0f + 27.0f * layout->notificationScale);
        break;
    case Element::ObjectiveBanner:
        layout->objectiveOffsetX = center.x - 320.0f;
        layout->objectiveOffsetY = center.y - 190.0f;
        break;
    case Element::Subtitles:
        layout->subtitleOffsetX = center.x - 320.0f;
        layout->subtitleOffsetY = center.y - 420.0f;
        break;
    default:
        break;
    }
    ClampLayout(layout);
}

bool HitTestElement(
    const Layout& layout,
    const Point point,
    Element* const element)
{
    if (element == nullptr)
    {
        return false;
    }
    for (std::size_t reverse = kElementCount; reverse > 0u; --reverse)
    {
        const Element candidate = static_cast<Element>(reverse - 1u);
        const Point center = ElementCenter(layout, candidate);
        const Size size = ElementSize(layout, candidate);
        if (point.x >= center.x - size.width * 0.5f &&
            point.x <= center.x + size.width * 0.5f &&
            point.y >= center.y - size.height * 0.5f &&
            point.y <= center.y + size.height * 0.5f)
        {
            *element = candidate;
            return true;
        }
    }
    return false;
}

const char* ResponseStatusId(const ResponseStatus status)
{
    switch (status)
    {
    case ResponseStatus::Active: return "ACTIVE";
    case ResponseStatus::Saved: return "SAVED";
    case ResponseStatus::Canceled: return "CANCELED";
    default: return "ERROR";
    }
}

bool ParseResponseStatus(
    const std::string& value,
    ResponseStatus* const status)
{
    if (status == nullptr)
    {
        return false;
    }
    const std::array<ResponseStatus, 4> statuses = {{
        ResponseStatus::Active,
        ResponseStatus::Saved,
        ResponseStatus::Canceled,
        ResponseStatus::Error,
    }};
    for (const ResponseStatus candidate : statuses)
    {
        if (value == ResponseStatusId(candidate))
        {
            *status = candidate;
            return true;
        }
    }
    return false;
}

std::string SerializeRequest(const Request& request)
{
    Layout layout = request.layout;
    ClampLayout(&layout);
    std::ostringstream output;
    output.setf(std::ios::fixed, std::ios::floatfield);
    output.precision(2);
    output << "VERSION=1\r\n";
    output << "REQUEST_ID=" << request.requestId << "\r\n";
    output << "COMMAND=START\r\n";
    WriteLayout(output, layout);
    return output.str();
}

bool ParseRequest(
    const std::string& text,
    Request* const request,
    std::string* const error)
{
    if (request == nullptr)
    {
        return false;
    }
    const auto values = ParseKeyValues(text, error);
    const auto version = values.find("VERSION");
    const auto requestId = values.find("REQUEST_ID");
    const auto command = values.find("COMMAND");
    Layout layout;
    if (values.empty() || version == values.end() || version->second != "1" ||
        requestId == values.end() || !IsSafeToken(requestId->second) ||
        command == values.end() || command->second != "START" ||
        !ParseLayoutValues(values, &layout) || !LayoutIsFinite(layout))
    {
        if (error != nullptr && error->empty())
        {
            *error = "HUD editor request is incomplete or invalid";
        }
        return false;
    }
    ClampLayout(&layout);
    request->requestId = requestId->second;
    request->layout = layout;
    return true;
}

std::string SerializeResponse(const Response& response)
{
    Layout layout = response.layout;
    ClampLayout(&layout);
    std::ostringstream output;
    output.setf(std::ios::fixed, std::ios::floatfield);
    output.precision(2);
    output << "VERSION=1\r\n";
    output << "STATUS=" << ResponseStatusId(response.status) << "\r\n";
    output << "REQUEST_ID=" << response.requestId << "\r\n";
    output << "MESSAGE=";
    for (const unsigned char character : response.message)
    {
        if (character >= 32u && character <= 126u && character != '=' &&
            character != '&' && character != '|' && character != '<' &&
            character != '>')
        {
            output << static_cast<char>(character);
        }
    }
    output << "\r\n";
    WriteLayout(output, layout);
    return output.str();
}

bool ParseResponse(
    const std::string& text,
    Response* const response,
    std::string* const error)
{
    if (response == nullptr)
    {
        return false;
    }
    const auto values = ParseKeyValues(text, error);
    const auto version = values.find("VERSION");
    const auto requestId = values.find("REQUEST_ID");
    const auto status = values.find("STATUS");
    const auto message = values.find("MESSAGE");
    Layout layout;
    ResponseStatus parsedStatus = ResponseStatus::Error;
    if (values.empty() || version == values.end() || version->second != "1" ||
        requestId == values.end() || !IsSafeToken(requestId->second) ||
        status == values.end() ||
        !ParseResponseStatus(status->second, &parsedStatus) ||
        message == values.end() || !ParseLayoutValues(values, &layout) ||
        !LayoutIsFinite(layout))
    {
        if (error != nullptr && error->empty())
        {
            *error = "HUD editor response is incomplete or invalid";
        }
        return false;
    }
    ClampLayout(&layout);
    response->requestId = requestId->second;
    response->status = parsedStatus;
    response->layout = layout;
    response->message = message->second;
    return true;
}

} // namespace kisak::vr::hud
