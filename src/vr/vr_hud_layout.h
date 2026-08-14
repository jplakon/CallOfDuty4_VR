#pragma once

#include <cstddef>
#include <string>

namespace kisak::vr::hud
{

constexpr float kCanvasWidth = 640.0f;
constexpr float kCanvasHeight = 480.0f;
constexpr float kMinimumScale = 0.50f;
constexpr float kMaximumScale = 2.00f;
constexpr float kSnapDistance = 14.0f;

enum class Element : std::size_t
{
    AmmoEquipment = 0u,
    Compass,
    Notifications,
    ObjectiveBanner,
    Subtitles,
    Count,
};

constexpr std::size_t kElementCount =
    static_cast<std::size_t>(Element::Count);

struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Size
{
    float width = 0.0f;
    float height = 0.0f;
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Layout
{
    float safeX = 0.50f;
    float safeY = 1.00f;

    float ammoOffsetX = 0.0f;
    float ammoOffsetY = 0.0f;
    float ammoScale = 0.50f;

    bool compassEnabled = true;
    float compassInsetX = 220.0f;
    float compassInsetY = 48.0f;
    float compassScale = 1.00f;

    float notificationOffsetX = 0.0f;
    float notificationOffsetY = 72.0f;
    float notificationScale = 1.00f;

    float objectiveOffsetX = 0.0f;
    float objectiveOffsetY = 0.0f;
    float objectiveScale = 1.00f;

    float subtitleOffsetX = 0.0f;
    float subtitleOffsetY = 0.0f;
    float subtitleScale = 1.00f;
};

enum class ResponseStatus
{
    Active,
    Saved,
    Canceled,
    Error,
};

struct Request
{
    std::string requestId;
    Layout layout;
};

struct Response
{
    std::string requestId;
    ResponseStatus status = ResponseStatus::Error;
    Layout layout;
    std::string message;
};

struct EditorSnapshot
{
    bool active = false;
    Layout layout;
    Element selected = Element::AmmoEquipment;
    Point pointer;
    bool pointerValid = false;
    bool dragging = false;
    bool snapEnabled = true;
};

Layout DefaultLayout();
void ClampLayout(Layout* layout);

const char* ElementId(Element element);
const char* ElementLabel(Element element);
bool ParseElement(const std::string& value, Element* element);
Element CycleElement(Element element, int direction);

Point SafeAreaMinimum(const Layout& layout);
Point SafeAreaMaximum(const Layout& layout);
Point ElementCenter(const Layout& layout, Element element);
Size ElementSize(const Layout& layout, Element element);
Rect TransformCompassRect(
    const Layout& layout,
    Rect sourceRect);
float ElementScale(const Layout& layout, Element element);
void SetElementScale(Layout* layout, Element element, float scale);
void CenterElement(Layout* layout, Element element);
void ResetElement(Layout* layout, Element element);
void MoveElement(
    Layout* layout,
    Element element,
    Point center,
    bool snapToGuides);
bool HitTestElement(
    const Layout& layout,
    Point point,
    Element* element);
Point SnapPointToGuides(
    const Layout& layout,
    Point point,
    float distance = kSnapDistance);

const char* ResponseStatusId(ResponseStatus status);
bool ParseResponseStatus(
    const std::string& value,
    ResponseStatus* status);

std::string SerializeRequest(const Request& request);
bool ParseRequest(
    const std::string& text,
    Request* request,
    std::string* error);

std::string SerializeResponse(const Response& response);
bool ParseResponse(
    const std::string& text,
    Response* response,
    std::string* error);

} // namespace kisak::vr::hud
