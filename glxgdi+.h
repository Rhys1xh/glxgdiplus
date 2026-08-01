// glxgdi+.h - Version 3.1
// A intuitive, header-only GDI+ helper library for Windows
// Just #include "glxgdi+.h" and you're ready to go!
//
// Features:
// - Zero-config initialization (auto GDI+ startup/shutdown)
// - RAII resource management with proper copy/move semantics
// - Method chaining for fluent API
// - Automatic double-buffering in Canvas
// - ~80% GDI+ feature coverage
// - Configurable error handling (throw/silent/log)
// - Thread-safe initialization
// - Fast pixel access via LockBits
// - Complete path, region, and transformation support
//
// Usage:
//   Canvas canvas(L"Title", 800, 600);
//   canvas.OnPaint([](Graphics& g) { ... });
//   canvas.Show();
//   Canvas::RunMessageLoop();

#pragma once
#ifndef GLXGDI_PLUS_H
#define GLXGDI_PLUS_H

// Standard Windows header guard
#if !defined(_WINDOWS_) && !defined(_WINDOWS) && !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(_WINDOWS_) && !defined(_WINDOWS)
#include <windows.h>
#endif

#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <cmath>
#include <stdexcept>
#include <atomic>
#include <stack>
#include <mutex>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")

namespace glx {

// ============================================================================
// Error handling configuration
// ============================================================================
enum class ErrorSeverity {
    Fatal,      // Operation failed critically
    Warning,    // Operation degraded but continued
    Info        // Non-critical issue
};

class GdiPlusException : public std::runtime_error {
private:
    ErrorSeverity m_severity;
    
public:
    GdiPlusException(const std::string& msg, ErrorSeverity severity = ErrorSeverity::Fatal) 
        : std::runtime_error(msg), m_severity(severity) {}
    
    ErrorSeverity GetSeverity() const { return m_severity; }
};

enum class ErrorMode {
    Throw,      // Throw exceptions on errors (default)
    Silent,     // Silently ignore errors
    LogOnly     // Log to debug output, don't throw
};

namespace detail {
    static ErrorMode g_errorMode = ErrorMode::Throw;
    
    inline void SetErrorMode(ErrorMode mode) { g_errorMode = mode; }
    inline ErrorMode GetErrorMode() { return g_errorMode; }
    
    // Verify GDI+ status helper with configurable behavior
    inline void CheckStatus(Gdiplus::Status status, const char* operation, ErrorSeverity severity = ErrorSeverity::Fatal) {
        if (status != Gdiplus::Ok) {
            char msg[256];
            sprintf_s(msg, sizeof(msg), "GDI+ operation failed: %s (status: %d)", operation, (int)status);
            
            switch (g_errorMode) {
                case ErrorMode::Throw:
                    throw GdiPlusException(msg, severity);
                case ErrorMode::LogOnly:
                    OutputDebugStringA(msg);
                    OutputDebugStringA("\n");
                    break;
                case ErrorMode::Silent:
                default:
                    break;
            }
        }
    }
}

// ============================================================================
// GDI+ Initialization - Thread-safe, reference counted, auto-cleanup
// ============================================================================
class GdiPlusManager {
private:
    ULONG_PTR m_token;
    Gdiplus::GdiplusStartupInput m_input;
    static std::atomic<LONG> s_refCount;
    static CRITICAL_SECTION s_cs;
    static bool s_csInitialized;
    
    GdiPlusManager() {
        Gdiplus::GdiplusStartup(&m_token, &m_input, nullptr);
    }
    
    ~GdiPlusManager() {
        Gdiplus::GdiplusShutdown(m_token);
    }
    
    static void EnsureCSInitialized() {
        if (!s_csInitialized) {
            InitializeCriticalSection(&s_cs);
            s_csInitialized = true;
        }
    }
    
public:
    static void Init() {
        EnsureCSInitialized();
        EnterCriticalSection(&s_cs);
        if (s_refCount == 0) {
            static GdiPlusManager instance;
            // Register cleanup handler for abnormal termination
            std::atexit([]() {
                EnterCriticalSection(&s_cs);
                while (s_refCount > 0) {
                    s_refCount--;
                }
                LeaveCriticalSection(&s_cs);
            });
        }
        s_refCount++;
        LeaveCriticalSection(&s_cs);
    }
    
    static void Shutdown() {
        EnsureCSInitialized();
        EnterCriticalSection(&s_cs);
        if (s_refCount > 0) {
            s_refCount--;
        }
        LeaveCriticalSection(&s_cs);
    }
    
    // RAII guard for scoped initialization
    class ScopedInit {
    public:
        ScopedInit() { GdiPlusManager::Init(); }
        ~ScopedInit() { GdiPlusManager::Shutdown(); }
    };
    
    struct AutoInit {
        AutoInit() { GdiPlusManager::Init(); }
    };
};

std::atomic<LONG> GdiPlusManager::s_refCount(0);
CRITICAL_SECTION GdiPlusManager::s_cs;
bool GdiPlusManager::s_csInitialized = false;

// Auto-initialize when header is included
namespace { 
    static GdiPlusManager::AutoInit auto_init; 
}

// ============================================================================
// Color - Standard RGBA color class
// ============================================================================
struct Color {
    BYTE r, g, b, a;
    
    // Components in RGBA parameter order (maps to GDI+ ARGB internally)
    Color(BYTE r = 0, BYTE g = 0, BYTE b = 0, BYTE a = 255) : r(r), g(g), b(b), a(a) {}
    
    explicit Color(Gdiplus::ARGB argb) 
        : a((argb >> 24) & 0xFF), r((argb >> 16) & 0xFF), 
          g((argb >> 8) & 0xFF), b(argb & 0xFF) {}
    
    explicit Color(const Gdiplus::Color& c) 
        : a(c.GetA()), r(c.GetR()), g(c.GetG()), b(c.GetB()) {}
    
    operator Gdiplus::Color() const { return Gdiplus::Color(a, r, g, b); }
    operator Gdiplus::ARGB() const { return Gdiplus::Color::MakeARGB(a, r, g, b); }
    
    // Named colors
    static Color Transparent() noexcept { return Color(0, 0, 0, 0); }
    static Color Black() noexcept { return Color(0, 0, 0); }
    static Color White() noexcept { return Color(255, 255, 255); }
    static Color Red() noexcept { return Color(255, 0, 0); }
    static Color Green() noexcept { return Color(0, 255, 0); }
    static Color Blue() noexcept { return Color(0, 0, 255); }
    static Color Yellow() noexcept { return Color(255, 255, 0); }
    static Color Cyan() noexcept { return Color(0, 255, 255); }
    static Color Magenta() noexcept { return Color(255, 0, 255); }
    static Color Gray(BYTE level = 128) noexcept { return Color(level, level, level); }
    static Color Orange() noexcept { return Color(255, 165, 0); }
    static Color Purple() noexcept { return Color(128, 0, 128); }
    static Color Pink() noexcept { return Color(255, 192, 203); }
    static Color Lime() noexcept { return Color(50, 205, 50); }
    static Color Teal() noexcept { return Color(0, 128, 128); }
    static Color Navy() noexcept { return Color(0, 0, 128); }
    static Color Brown() noexcept { return Color(165, 42, 42); }
    static Color Silver() noexcept { return Color(192, 192, 192); }
    static Color Gold() noexcept { return Color(255, 215, 0); }
    
    // Color manipulation
    Color WithAlpha(BYTE newAlpha) const noexcept { return Color(r, g, b, newAlpha); }
    
    Color Lighten(float factor) const noexcept {
        factor = std::max(0.0f, std::min(1.0f, factor));
        return Color(
            (BYTE)(r + (255 - r) * factor),
            (BYTE)(g + (255 - g) * factor),
            (BYTE)(b + (255 - b) * factor),
            a
        );
    }
    
    Color Darken(float factor) const noexcept {
        factor = std::max(0.0f, std::min(1.0f, factor));
        return Color(
            (BYTE)(r * (1.0f - factor)),
            (BYTE)(g * (1.0f - factor)),
            (BYTE)(b * (1.0f - factor)),
            a
        );
    }
    
    Color Blend(const Color& other, float t) const noexcept {
        t = std::max(0.0f, std::min(1.0f, t));
        return Color(
            (BYTE)(r + (other.r - r) * t),
            (BYTE)(g + (other.g - g) * t),
            (BYTE)(b + (other.b - b) * t),
            (BYTE)(a + (other.a - a) * t)
        );
    }
    
    bool operator==(const Color& other) const noexcept {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    
    bool operator!=(const Color& other) const noexcept {
        return !(*this == other);
    }
};

// ============================================================================
// Point - 2D point with math operations
// ============================================================================
struct Point {
    float x, y;
    
    Point(float x = 0, float y = 0) noexcept : x(x), y(y) {}
    explicit Point(const Gdiplus::PointF& pt) noexcept : x(pt.X), y(pt.Y) {}
    explicit Point(const Gdiplus::Point& pt) noexcept : x((float)pt.X), y((float)pt.Y) {}
    explicit Point(const POINT& pt) noexcept : x((float)pt.x), y((float)pt.y) {}
    
    operator Gdiplus::PointF() const noexcept { return Gdiplus::PointF(x, y); }
    operator Gdiplus::Point() const noexcept { return Gdiplus::Point((INT)x, (INT)y); }
    operator POINT() const noexcept { return { (LONG)x, (LONG)y }; }
    
    Point operator+(const Point& other) const noexcept { return Point(x + other.x, y + other.y); }
    Point operator-(const Point& other) const noexcept { return Point(x - other.x, y - other.y); }
    Point operator*(float scale) const noexcept { return Point(x * scale, y * scale); }
    Point operator/(float scale) const noexcept { return Point(x / scale, y / scale); }
    Point operator-() const noexcept { return Point(-x, -y); }
    
    Point& operator+=(const Point& other) noexcept { x += other.x; y += other.y; return *this; }
    Point& operator-=(const Point& other) noexcept { x -= other.x; y -= other.y; return *this; }
    Point& operator*=(float scale) noexcept { x *= scale; y *= scale; return *this; }
    Point& operator/=(float scale) noexcept { x /= scale; y /= scale; return *this; }
    
    bool operator==(const Point& other) const noexcept { return x == other.x && y == other.y; }
    bool operator!=(const Point& other) const noexcept { return !(*this == other); }
    
    float Length() const noexcept { return std::sqrt(x * x + y * y); }
    
    float Distance(const Point& other) const noexcept {
        float dx = x - other.x, dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    Point Normalized() const noexcept {
        float len = Length();
        return len > 0 ? Point(x / len, y / len) : Point();
    }
    
    float Dot(const Point& other) const noexcept { return x * other.x + y * other.y; }
    float Cross(const Point& other) const noexcept { return x * other.y - y * other.x; }
};

// ============================================================================
// Size - 2D size helper
// ============================================================================
struct Size {
    float width, height;
    
    Size(float w = 0, float h = 0) noexcept : width(w), height(h) {}
    explicit Size(const Gdiplus::SizeF& sz) noexcept : width(sz.Width), height(sz.Height) {}
    
    operator Gdiplus::SizeF() const noexcept { return Gdiplus::SizeF(width, height); }
    
    bool operator==(const Size& other) const noexcept { return width == other.width && height == other.height; }
    bool IsEmpty() const noexcept { return width <= 0 || height <= 0; }
};

// ============================================================================
// Rect - Rectangle with utility methods
// ============================================================================
struct Rect {
    float x, y, width, height;
    
    Rect(float x = 0, float y = 0, float w = 0, float h = 0) noexcept : x(x), y(y), width(w), height(h) {}
    explicit Rect(const Gdiplus::RectF& rc) noexcept : x(rc.X), y(rc.Y), width(rc.Width), height(rc.Height) {}
    explicit Rect(const Gdiplus::Rect& rc) noexcept : x((float)rc.X), y((float)rc.Y), width((float)rc.Width), height((float)rc.Height) {}
    explicit Rect(const RECT& rc) noexcept : x((float)rc.left), y((float)rc.top), 
                                              width((float)(rc.right - rc.left)), 
                                              height((float)(rc.bottom - rc.top)) {}
    Rect(const Point& topLeft, const Size& sz) noexcept : x(topLeft.x), y(topLeft.y), width(sz.width), height(sz.height) {}
    
    operator Gdiplus::RectF() const noexcept { return Gdiplus::RectF(x, y, width, height); }
    operator Gdiplus::Rect() const noexcept { return Gdiplus::Rect((INT)x, (INT)y, (INT)width, (INT)height); }
    operator RECT() const noexcept { return { (LONG)x, (LONG)y, (LONG)(x + width), (LONG)(y + height) }; }
    
    float Left() const noexcept { return x; }
    float Top() const noexcept { return y; }
    float Right() const noexcept { return x + width; }
    float Bottom() const noexcept { return y + height; }
    Point Center() const noexcept { return Point(x + width / 2, y + height / 2); }
    Point TopLeft() const noexcept { return Point(x, y); }
    Point TopRight() const noexcept { return Point(Right(), y); }
    Point BottomLeft() const noexcept { return Point(x, Bottom()); }
    Point BottomRight() const noexcept { return Point(Right(), Bottom()); }
    Size GetSize() const noexcept { return Size(width, height); }
    
    bool Contains(const Point& pt) const noexcept { 
        return pt.x >= x && pt.x <= Right() && pt.y >= y && pt.y <= Bottom(); 
    }
    
    bool Contains(const Rect& other) const noexcept {
        return other.x >= x && other.Right() <= Right() && 
               other.y >= y && other.Bottom() <= Bottom();
    }
    
    bool Intersects(const Rect& other) const noexcept {
        return !(x > other.Right() || Right() < other.x || 
                 y > other.Bottom() || Bottom() < other.y);
    }
    
    Rect Intersect(const Rect& other) const noexcept {
        float newX = std::max(x, other.x);
        float newY = std::max(y, other.y);
        float newRight = std::min(Right(), other.Right());
        float newBottom = std::min(Bottom(), other.Bottom());
        
        if (newRight > newX && newBottom > newY) {
            return Rect(newX, newY, newRight - newX, newBottom - newY);
        }
        return Rect();
    }
    
    Rect Union(const Rect& other) const noexcept {
        float newX = std::min(x, other.x);
        float newY = std::min(y, other.y);
        float newRight = std::max(Right(), other.Right());
        float newBottom = std::max(Bottom(), other.Bottom());
        return Rect(newX, newY, newRight - newX, newBottom - newY);
    }
    
    Rect Inflate(float dx, float dy) const noexcept {
        return Rect(x - dx, y - dy, width + 2 * dx, height + 2 * dy);
    }
    
    Rect Inflate(float d) const noexcept {
        return Inflate(d, d);
    }
    
    Rect Offset(float dx, float dy) const noexcept {
        return Rect(x + dx, y + dy, width, height);
    }
    
    bool IsEmpty() const noexcept { return width <= 0 || height <= 0; }
    
    static Rect FromCenter(const Point& center, float width, float height) noexcept {
        return Rect(center.x - width / 2, center.y - height / 2, width, height);
    }
    
    static Rect FromPoints(const Point& p1, const Point& p2) noexcept {
        float x = std::min(p1.x, p2.x);
        float y = std::min(p1.y, p2.y);
        float w = std::abs(p2.x - p1.x);
        float h = std::abs(p2.y - p1.y);
        return Rect(x, y, w, h);
    }
    
    static Rect FromLTRB(float left, float top, float right, float bottom) noexcept {
        return Rect(left, top, right - left, bottom - top);
    }
    
    bool operator==(const Rect& other) const noexcept {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

// ============================================================================
// Pen - Copy-safe, move-safe pen wrapper
// ============================================================================
class Pen {
private:
    std::unique_ptr<Gdiplus::Pen> m_pen;
    
public:
    Pen(const Color& color, float width = 1.0f) 
        : m_pen(std::make_unique<Gdiplus::Pen>(Gdiplus::Color(color.a, color.r, color.g, color.b), width)) {
        if (!m_pen) throw GdiPlusException("Failed to create Pen");
    }
    
    // Move semantics
    Pen(Pen&&) noexcept = default;
    Pen& operator=(Pen&&) noexcept = default;
    
    // Copy via Clone
    Pen(const Pen& other) {
        if (other.m_pen) {
            m_pen.reset(other.m_pen->Clone());
        }
    }
    
    Pen& operator=(const Pen& other) {
        if (this != &other && other.m_pen) {
            m_pen.reset(other.m_pen->Clone());
        }
        return *this;
    }
    
    // Property setters with method chaining
    Pen& SetWidth(float width) noexcept { 
        if (m_pen) m_pen->SetWidth(width);
        return *this; 
    }
    
    Pen& SetColor(const Color& color) noexcept { 
        if (m_pen) m_pen->SetColor(Gdiplus::Color(color.a, color.r, color.g, color.b));
        return *this; 
    }
    
    Pen& SetDashStyle(Gdiplus::DashStyle style) noexcept { 
        if (m_pen) m_pen->SetDashStyle(style);
        return *this; 
    }
    
    Pen& SetDashPattern(const std::vector<float>& pattern, float offset = 0) noexcept {
        if (m_pen && !pattern.empty()) {
            m_pen->SetDashPattern(pattern.data(), (INT)pattern.size());
            m_pen->SetDashOffset(offset);
        }
        return *this;
    }
    
    Pen& SetStartCap(Gdiplus::LineCap cap) noexcept { 
        if (m_pen) m_pen->SetStartCap(cap);
        return *this; 
    }
    
    Pen& SetEndCap(Gdiplus::LineCap cap) noexcept { 
        if (m_pen) m_pen->SetEndCap(cap);
        return *this; 
    }
    
    Pen& SetLineCap(Gdiplus::LineCap startCap, Gdiplus::LineCap endCap, Gdiplus::DashCap dashCap = Gdiplus::DashCapFlat) noexcept {
        if (m_pen) {
            m_pen->SetStartCap(startCap);
            m_pen->SetEndCap(endCap);
            m_pen->SetDashCap(dashCap);
        }
        return *this;
    }
    
    Pen& SetAlignment(Gdiplus::PenAlignment alignment) noexcept {
        if (m_pen) m_pen->SetAlignment(alignment);
        return *this;
    }
    
    Pen& SetMiterLimit(float limit) noexcept {
        if (m_pen) m_pen->SetMiterLimit(limit);
        return *this;
    }
    
    Pen& SetLineJoin(Gdiplus::LineJoin join) noexcept {
        if (m_pen) m_pen->SetLineJoin(join);
        return *this;
    }
    
    // Property getters
    float GetWidth() const noexcept { return m_pen ? m_pen->GetWidth() : 0.0f; }
    Gdiplus::DashStyle GetDashStyle() const noexcept { return m_pen ? m_pen->GetDashStyle() : Gdiplus::DashStyleSolid; }
    
    Gdiplus::Pen* Get() const noexcept { return m_pen.get(); }
    bool IsValid() const noexcept { return m_pen != nullptr; }
    
    // Predefined pens
    static Pen Black(float width = 1.0f) { return Pen(Color::Black(), width); }
    static Pen White(float width = 1.0f) { return Pen(Color::White(), width); }
    static Pen Red(float width = 1.0f) { return Pen(Color::Red(), width); }
    static Pen Green(float width = 1.0f) { return Pen(Color::Green(), width); }
    static Pen Blue(float width = 1.0f) { return Pen(Color::Blue(), width); }
};

// ============================================================================
// Brush - Common brush base with proper cloning
// ============================================================================
class Brush {
protected:
    std::unique_ptr<Gdiplus::Brush> m_brush;
    
    virtual std::unique_ptr<Brush> DoClone() const = 0;
    
public:
    Brush() = default;
    explicit Brush(std::unique_ptr<Gdiplus::Brush> brush) : m_brush(std::move(brush)) {}
    virtual ~Brush() = default;
    
    // Move semantics
    Brush(Brush&&) noexcept = default;
    Brush& operator=(Brush&&) noexcept = default;
    
    // Copy via virtual clone
    Brush(const Brush& other) {
        if (other.m_brush) {
            auto cloned = other.DoClone();
            if (cloned) m_brush = std::move(cloned->m_brush);
        }
    }
    
    Brush& operator=(const Brush& other) {
        if (this != &other) {
            if (other.m_brush) {
                auto cloned = other.DoClone();
                if (cloned) m_brush = std::move(cloned->m_brush);
            } else {
                m_brush.reset();
            }
        }
        return *this;
    }
    
    Gdiplus::Brush* Get() const noexcept { return m_brush.get(); }
    bool IsValid() const noexcept { return m_brush != nullptr; }
    
    // Factory methods
    static std::unique_ptr<Brush> Solid(const Color& color);
    static std::unique_ptr<Brush> LinearGradient(const Rect& rect, const Color& c1, const Color& c2, float angle = 0);
    static std::unique_ptr<Brush> Hatch(Gdiplus::HatchStyle style, const Color& fore, const Color& back);
    static std::unique_ptr<Brush> Texture(Image* image);
};

class SolidBrush : public Brush {
public:
    explicit SolidBrush(const Color& color) 
        : Brush(std::make_unique<Gdiplus::SolidBrush>(Gdiplus::Color(color.a, color.r, color.g, color.b))) {
        if (!m_brush) throw GdiPlusException("Failed to create SolidBrush");
    }
    
protected:
    std::unique_ptr<Brush> DoClone() const override {
        if (auto* sb = dynamic_cast<Gdiplus::SolidBrush*>(m_brush.get())) {
            Gdiplus::Color color;
            sb->GetColor(&color);
            return std::make_unique<SolidBrush>(Color(color));
        }
        return nullptr;
    }
};

class LinearGradientBrush : public Brush {
private:
    Rect m_rect;
    Color m_color1, m_color2;
    float m_angle;
    
public:
    LinearGradientBrush(const Rect& rect, const Color& color1, const Color& color2, float angle = 0.0f)
        : Brush(std::make_unique<Gdiplus::LinearGradientBrush>(
            Gdiplus::RectF(rect.x, rect.y, rect.width, rect.height),
            Gdiplus::Color(color1.a, color1.r, color1.g, color1.b),
            Gdiplus::Color(color2.a, color2.r, color2.g, color2.b),
            angle))
        , m_rect(rect), m_color1(color1), m_color2(color2), m_angle(angle) {
        if (!m_brush) throw GdiPlusException("Failed to create LinearGradientBrush");
    }
    
    LinearGradientBrush& SetBlend(const std::vector<float>& positions, const std::vector<Color>& colors) {
        if (auto* lgb = dynamic_cast<Gdiplus::LinearGradientBrush*>(m_brush.get())) {
            if (positions.size() == colors.size() && !positions.empty()) {
                std::vector<Gdiplus::Color> gdipColors;
                gdipColors.reserve(colors.size());
                for (const auto& c : colors) {
                    gdipColors.emplace_back(c.a, c.r, c.g, c.b);
                }
                lgb->SetInterpolationColors(gdipColors.data(), positions.data(), (INT)positions.size());
            }
        }
        return *this;
    }
    
protected:
    std::unique_ptr<Brush> DoClone() const override {
        return std::make_unique<LinearGradientBrush>(m_rect, m_color1, m_color2, m_angle);
    }
};

class HatchBrush : public Brush {
private:
    Gdiplus::HatchStyle m_style;
    Color m_foreColor, m_backColor;
    
public:
    HatchBrush(Gdiplus::HatchStyle style, const Color& foreColor, const Color& backColor)
        : Brush(std::make_unique<Gdiplus::HatchBrush>(
            style,
            Gdiplus::Color(foreColor.a, foreColor.r, foreColor.g, foreColor.b),
            Gdiplus::Color(backColor.a, backColor.r, backColor.g, backColor.b)))
        , m_style(style), m_foreColor(foreColor), m_backColor(backColor) {
        if (!m_brush) throw GdiPlusException("Failed to create HatchBrush");
    }
    
protected:
    std::unique_ptr<Brush> DoClone() const override {
        return std::make_unique<HatchBrush>(m_style, m_foreColor, m_backColor);
    }
};

class TextureBrush : public Brush {
public:
    explicit TextureBrush(Image* image) 
        : Brush(std::make_unique<Gdiplus::TextureBrush>(image ? image->Get() : nullptr)) {
        if (!m_brush) throw GdiPlusException("Failed to create TextureBrush");
    }
    
protected:
    std::unique_ptr<Brush> DoClone() const override {
        // TextureBrush doesn't support simple cloning via stored parameters
        // Return a new TextureBrush from the original image pointer
        return nullptr; // Users should recreate from source
    }
};

// Brush factory implementations
inline std::unique_ptr<Brush> Brush::Solid(const Color& color) {
    return std::make_unique<SolidBrush>(color);
}

inline std::unique_ptr<Brush> Brush::LinearGradient(const Rect& rect, const Color& c1, const Color& c2, float angle) {
    return std::make_unique<LinearGradientBrush>(rect, c1, c2, angle);
}

inline std::unique_ptr<Brush> Brush::Hatch(Gdiplus::HatchStyle style, const Color& fore, const Color& back) {
    return std::make_unique<HatchBrush>(style, fore, back);
}

inline std::unique_ptr<Brush> Brush::Texture(Image* image) {
    return std::make_unique<TextureBrush>(image);
}

// ============================================================================
// Font - Enhanced font handling with style helpers
// ============================================================================
class Font {
private:
    std::unique_ptr<Gdiplus::Font> m_font;
    std::wstring m_familyName;
    float m_size;
    int m_style;
    Gdiplus::Unit m_unit;
    
public:
    Font(const std::wstring& familyName, float size, int style = Gdiplus::FontStyleRegular, 
         Gdiplus::Unit unit = Gdiplus::UnitPoint)
        : m_font(std::make_unique<Gdiplus::Font>(familyName.c_str(), size, style, unit))
        , m_familyName(familyName), m_size(size), m_style(style), m_unit(unit) {
        if (!m_font) throw GdiPlusException("Failed to create Font");
    }
    
    Font(const wchar_t* familyName, float size, int style = Gdiplus::FontStyleRegular)
        : Font(std::wstring(familyName), size, style) {}
    
    // Move semantics
    Font(Font&&) noexcept = default;
    Font& operator=(Font&&) noexcept = default;
    
    // Copy constructor
    Font(const Font& other) 
        : m_font(std::make_unique<Gdiplus::Font>(other.m_familyName.c_str(), other.m_size, other.m_style, other.m_unit))
        , m_familyName(other.m_familyName), m_size(other.m_size), m_style(other.m_style), m_unit(other.m_unit) {}
    
    Font& operator=(const Font& other) {
        if (this != &other) {
            m_font = std::make_unique<Gdiplus::Font>(other.m_familyName.c_str(), other.m_size, other.m_style, other.m_unit);
            m_familyName = other.m_familyName;
            m_size = other.m_size;
            m_style = other.m_style;
            m_unit = other.m_unit;
        }
        return *this;
    }
    
    Gdiplus::Font* Get() const noexcept { return m_font.get(); }
    float GetSize() const noexcept { return m_size; }
    std::wstring GetFamily() const noexcept { return m_familyName; }
    int GetStyle() const noexcept { return m_style; }
    
    // Style helpers (non-destructive derivation)
    Font WithSize(float size) const { return Font(m_familyName, size, m_style, m_unit); }
    Font WithStyle(int style) const { return Font(m_familyName, m_size, style, m_unit); }
    Font Bold() const { return Font(m_familyName, m_size, m_style | Gdiplus::FontStyleBold, m_unit); }
    Font Italic() const { return Font(m_familyName, m_size, m_style | Gdiplus::FontStyleItalic, m_unit); }
    Font Underline() const { return Font(m_familyName, m_size, m_style | Gdiplus::FontStyleUnderline, m_unit); }
    Font Strikeout() const { return Font(m_familyName, m_size, m_style | Gdiplus::FontStyleStrikeout, m_unit); }
    Font Regular() const { return Font(m_familyName, m_size, Gdiplus::FontStyleRegular, m_unit); }
    
    bool IsBold() const noexcept { return (m_style & Gdiplus::FontStyleBold) != 0; }
    bool IsItalic() const noexcept { return (m_style & Gdiplus::FontStyleItalic) != 0; }
    
    // Predefined fonts
    static Font Default(float size = 12.0f) { return Font(L"Segoe UI", size); }
    static Font System(float size = 12.0f) { return Font(L"Arial", size); }
    static Font Monospace(float size = 12.0f) { return Font(L"Consolas", size); }
    static Font Serif(float size = 12.0f) { return Font(L"Times New Roman", size); }
};

// ============================================================================
// Image - Copy-safe image wrapper
// ============================================================================
class Image {
private:
    std::unique_ptr<Gdiplus::Image> m_image;
    
    static std::wstring ToWString(const std::string& str) noexcept {
        if (str.empty()) return std::wstring();
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0) return std::wstring();
        std::wstring result(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
        return result;
    }
    
public:
    Image() = default;
    
    explicit Image(const std::wstring& filename) {
        Load(filename);
    }
    
    explicit Image(const std::string& filename) {
        Load(filename);
    }
    
    // Move semantics
    Image(Image&&) noexcept = default;
    Image& operator=(Image&&) noexcept = default;
    
    // Copy via Clone
    Image(const Image& other) {
        if (other.m_image) {
            m_image.reset(other.m_image->Clone());
        }
    }
    
    Image& operator=(const Image& other) {
        if (this != &other) {
            m_image.reset(other.m_image ? other.m_image->Clone() : nullptr);
        }
        return *this;
    }
    
    bool Load(const std::wstring& filename) {
        auto img = std::make_unique<Gdiplus::Image>(filename.c_str());
        if (img && img->GetLastStatus() == Gdiplus::Ok) {
            m_image = std::move(img);
            return true;
        }
        return false;
    }
    
    bool Load(const std::string& filename) {
        return Load(ToWString(filename));
    }
    
    bool LoadFromResource(HINSTANCE hInstance, const std::wstring& resName, const std::wstring& resType = L"PNG") {
        HRSRC hResource = FindResourceW(hInstance, resName.c_str(), resType.c_str());
        if (!hResource) return false;
        
        DWORD size = SizeofResource(hInstance, hResource);
        if (size == 0) return false;
        
        HGLOBAL hGlobal = LoadResource(hInstance, hResource);
        if (!hGlobal) return false;
        
        void* data = LockResource(hGlobal);
        if (!data) return false;
        
        HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hBuffer) return false;
        
        void* buffer = GlobalLock(hBuffer);
        if (!buffer) {
            GlobalFree(hBuffer);
            return false;
        }
        
        memcpy(buffer, data, size);
        GlobalUnlock(hBuffer);
        
        IStream* stream = nullptr;
        HRESULT hr = CreateStreamOnHGlobal(hBuffer, TRUE, &stream);
        if (FAILED(hr) || !stream) {
            GlobalFree(hBuffer);
            return false;
        }
        
        auto img = std::make_unique<Gdiplus::Image>(stream);
        stream->Release(); // Stream is still valid, Image holds a reference
        
        if (img && img->GetLastStatus() == Gdiplus::Ok) {
            m_image = std::move(img);
            return true;
        }
        return false;
    }
    
    float GetWidth() const noexcept { return m_image ? (float)m_image->GetWidth() : 0.0f; }
    float GetHeight() const noexcept { return m_image ? (float)m_image->GetHeight() : 0.0f; }
    Size GetSize() const noexcept { return Size(GetWidth(), GetHeight()); }
    Gdiplus::Image* Get() const noexcept { return m_image.get(); }
    bool IsValid() const noexcept { return m_image != nullptr; }
    explicit operator bool() const noexcept { return IsValid(); }
};

// ============================================================================
// Bitmap - For creating and manipulating bitmaps with LockBits support
// ============================================================================
class Bitmap {
private:
    std::unique_ptr<Gdiplus::Bitmap> m_bitmap;
    
    static CLSID GetEncoderClsid(const std::wstring& format) {
        static std::map<std::wstring, CLSID> encoders;
        static std::once_flag initFlag;
        
        std::call_once(initFlag, []() {
            UINT num = 0, size = 0;
            Gdiplus::GetImageEncodersSize(&num, &size);
            if (size == 0) return;
            
            std::vector<BYTE> buffer(size);
            Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buffer[0]);
            Gdiplus::GetImageEncoders(num, size, codecs);
            
            for (UINT i = 0; i < num; i++) {
                encoders[codecs[i].MimeType] = codecs[i].Clsid;
            }
        });
        
        auto it = encoders.find(format);
        return (it != encoders.end()) ? it->second : CLSID();
    }
    
public:
    Bitmap(int width, int height) 
        : m_bitmap(std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB)) {
        if (!m_bitmap) throw GdiPlusException("Failed to create Bitmap");
    }
    
    explicit Bitmap(const std::wstring& filename) 
        : m_bitmap(std::make_unique<Gdiplus::Bitmap>(filename.c_str())) {
        if (!m_bitmap || m_bitmap->GetLastStatus() != Gdiplus::Ok)
            throw GdiPlusException("Failed to load Bitmap from file");
    }
    
    // Move semantics
    Bitmap(Bitmap&&) noexcept = default;
    Bitmap& operator=(Bitmap&&) noexcept = default;
    
    // Copy via Clone
    Bitmap(const Bitmap& other) {
        if (other.m_bitmap) {
            m_bitmap.reset(other.m_bitmap->Clone(0, 0, other.m_bitmap->GetWidth(), 
                                                  other.m_bitmap->GetHeight(), PixelFormat32bppARGB));
        }
    }
    
    Bitmap& operator=(const Bitmap& other) {
        if (this != &other) {
            m_bitmap.reset(other.m_bitmap ? other.m_bitmap->Clone(0, 0, other.m_bitmap->GetWidth(),
                                                                    other.m_bitmap->GetHeight(), 
                                                                    PixelFormat32bppARGB) : nullptr);
        }
        return *this;
    }
    
    // Pixel access
    void SetPixel(int x, int y, const Color& color) noexcept {
        if (m_bitmap) {
            Gdiplus::Color c(color.a, color.r, color.g, color.b);
            m_bitmap->SetPixel(x, y, c);
        }
    }
    
    Color GetPixel(int x, int y) const noexcept {
        Gdiplus::Color c;
        if (m_bitmap) {
            m_bitmap->GetPixel(x, y, &c);
        }
        return Color(c.GetR(), c.GetG(), c.GetB(), c.GetA());
    }
    
    // Fast pixel access via LockBits
    class BitmapData {
    private:
        Gdiplus::Bitmap* m_bitmap;
        Gdiplus::BitmapData m_data;
        bool m_locked;
        
    public:
        BitmapData(Gdiplus::Bitmap* bitmap, const Rect& rect, bool readOnly = false) 
            : m_bitmap(bitmap), m_locked(false) {
            if (m_bitmap) {
                Gdiplus::Rect gdipRect((INT)rect.x, (INT)rect.y, (INT)rect.width, (INT)rect.height);
                Gdiplus::Status status = m_bitmap->LockBits(
                    &gdipRect,
                    readOnly ? Gdiplus::ImageLockModeRead : Gdiplus::ImageLockModeWrite,
                    PixelFormat32bppARGB,
                    &m_data
                );
                m_locked = (status == Gdiplus::Ok);
            }
        }
        
        ~BitmapData() {
            if (m_locked && m_bitmap) {
                m_bitmap->UnlockBits(&m_data);
            }
        }
        
        // Non-copyable, movable
        BitmapData(const BitmapData&) = delete;
        BitmapData& operator=(const BitmapData&) = delete;
        BitmapData(BitmapData&& other) noexcept 
            : m_bitmap(other.m_bitmap), m_data(other.m_data), m_locked(other.m_locked) {
            other.m_bitmap = nullptr;
            other.m_locked = false;
        }
        
        bool IsLocked() const noexcept { return m_locked; }
        BYTE* GetScan0() const noexcept { return m_locked ? static_cast<BYTE*>(m_data.Scan0) : nullptr; }
        int GetStride() const noexcept { return m_locked ? (int)std::abs(m_data.Stride) : 0; }
        int GetWidth() const noexcept { return m_locked ? (int)m_data.Width : 0; }
        int GetHeight() const noexcept { return m_locked ? (int)m_data.Height : 0; }
        int GetPixelFormat() const noexcept { return m_locked ? (int)m_data.PixelFormat : 0; }
        
        void SetPixel(int x, int y, const Color& color) noexcept {
            if (!m_locked || !m_data.Scan0) return;
            if (x < 0 || x >= (int)m_data.Width || y < 0 || y >= (int)m_data.Height) return;
            
            // Handle stride independently (may include padding)
            BYTE* row = static_cast<BYTE*>(m_data.Scan0) + y * std::abs(m_data.Stride);
            BYTE* pixel = row + x * 4;
            // GDI+ uses BGRA in memory for PixelFormat32bppARGB
            pixel[0] = color.b;
            pixel[1] = color.g;
            pixel[2] = color.r;
            pixel[3] = color.a;
        }
        
        Color GetPixel(int x, int y) const noexcept {
            if (!m_locked || !m_data.Scan0) return Color();
            if (x < 0 || x >= (int)m_data.Width || y < 0 || y >= (int)m_data.Height) return Color();
            
            BYTE* row = static_cast<BYTE*>(m_data.Scan0) + y * std::abs(m_data.Stride);
            BYTE* pixel = row + x * 4;
            return Color(pixel[2], pixel[1], pixel[0], pixel[3]); // BGRA -> RGBA
        }
    };
    
    BitmapData LockBits(const Rect& rect, bool readOnly = false) {
        return BitmapData(m_bitmap.get(), rect, readOnly);
    }
    
    int GetWidth() const noexcept { return m_bitmap ? m_bitmap->GetWidth() : 0; }
    int GetHeight() const noexcept { return m_bitmap ? m_bitmap->GetHeight() : 0; }
    Size GetSize() const noexcept { return Size((float)GetWidth(), (float)GetHeight()); }
    Gdiplus::Bitmap* Get() const noexcept { return m_bitmap.get(); }
    
    // Save with automatic format detection
    bool Save(const std::wstring& filename) const {
        if (!m_bitmap) return false;
        
        std::wstring ext;
        size_t dotPos = filename.find_last_of(L'.');
        if (dotPos != std::wstring::npos) {
            ext = filename.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        }
        
        std::wstring mimeType = L"image/png"; // default
        if (ext == L"jpg" || ext == L"jpeg") mimeType = L"image/jpeg";
        else if (ext == L"bmp") mimeType = L"image/bmp";
        else if (ext == L"gif") mimeType = L"image/gif";
        else if (ext == L"tif" || ext == L"tiff") mimeType = L"image/tiff";
        
        CLSID clsid = GetEncoderClsid(mimeType);
        if (clsid == CLSID()) return false;
        
        Gdiplus::Status status = m_bitmap->Save(filename.c_str(), &clsid);
        return status == Gdiplus::Ok;
    }
    
    bool Save(const std::wstring& filename, long jpegQuality) const {
        if (!m_bitmap) return false;
        
        CLSID clsid = GetEncoderClsid(L"image/jpeg");
        if (clsid == CLSID()) return false;
        
        Gdiplus::EncoderParameters params;
        params.Count = 1;
        params.Parameter[0].Guid = Gdiplus::EncoderQuality;
        params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        params.Parameter[0].Value = &jpegQuality;
        
        Gdiplus::Status status = m_bitmap->Save(filename.c_str(), &clsid, &params);
        return status == Gdiplus::Ok;
    }
    
    // Resize
    Bitmap Resize(int newWidth, int newHeight) const {
        Bitmap result(newWidth, newHeight);
        if (m_bitmap && result.m_bitmap) {
            Gdiplus::Graphics g(result.m_bitmap.get());
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(m_bitmap.get(), 0, 0, newWidth, newHeight);
        }
        return result;
    }
    
    // Rotate/Flip
    Bitmap RotateFlip(Gdiplus::RotateFlipType type) const {
        Bitmap result(*this);
        if (result.m_bitmap) {
            result.m_bitmap->RotateFlip(type);
        }
        return result;
    }
};

// ============================================================================
// GraphicsPath - Path construction and drawing
// ============================================================================
class GraphicsPath {
private:
    std::unique_ptr<Gdiplus::GraphicsPath> m_path;
    
public:
    GraphicsPath() : m_path(std::make_unique<Gdiplus::GraphicsPath>()) {
        if (!m_path) throw GdiPlusException("Failed to create GraphicsPath");
    }
    
    GraphicsPath(const GraphicsPath& other) {
        if (other.m_path) {
            m_path.reset(other.m_path->Clone());
        }
    }
    
    GraphicsPath& operator=(const GraphicsPath& other) {
        if (this != &other) {
            m_path.reset(other.m_path ? other.m_path->Clone() : nullptr);
        }
        return *this;
    }
    
    GraphicsPath(GraphicsPath&&) noexcept = default;
    GraphicsPath& operator=(GraphicsPath&&) noexcept = default;
    
    Gdiplus::GraphicsPath* Get() const noexcept { return m_path.get(); }
    
    // Figure management
    GraphicsPath& StartFigure() { 
        if (m_path) detail::CheckStatus(m_path->StartFigure(), "GraphicsPath::StartFigure");
        return *this; 
    }
    
    GraphicsPath& CloseFigure() { 
        if (m_path) detail::CheckStatus(m_path->CloseFigure(), "GraphicsPath::CloseFigure");
        return *this; 
    }
    
    GraphicsPath& CloseAllFigures() {
        if (m_path) detail::CheckStatus(m_path->CloseAllFigures(), "GraphicsPath::CloseAllFigures");
        return *this;
    }
    
    // Line operations
    GraphicsPath& AddLine(const Point& p1, const Point& p2) {
        if (m_path) detail::CheckStatus(m_path->AddLine(p1, p2), "GraphicsPath::AddLine");
        return *this;
    }
    
    GraphicsPath& AddLines(const std::vector<Point>& points) {
        if (m_path && points.size() >= 2) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_path->AddLines(gdipPoints.data(), (INT)gdipPoints.size()), 
                               "GraphicsPath::AddLines");
        }
        return *this;
    }
    
    // Arc and curve operations
    GraphicsPath& AddArc(const Rect& rect, float startAngle, float sweepAngle) {
        if (m_path) detail::CheckStatus(m_path->AddArc(rect, startAngle, sweepAngle), "GraphicsPath::AddArc");
        return *this;
    }
    
    GraphicsPath& AddArc(float x, float y, float width, float height, float startAngle, float sweepAngle) {
        return AddArc(Rect(x, y, width, height), startAngle, sweepAngle);
    }
    
    GraphicsPath& AddBezier(const Point& p1, const Point& p2, const Point& p3, const Point& p4) {
        if (m_path) detail::CheckStatus(m_path->AddBezier(p1, p2, p3, p4), "GraphicsPath::AddBezier");
        return *this;
    }
    
    GraphicsPath& AddBeziers(const std::vector<Point>& points) {
        if (m_path && points.size() >= 4) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_path->AddBeziers(gdipPoints.data(), (INT)gdipPoints.size()),
                               "GraphicsPath::AddBeziers");
        }
        return *this;
    }
    
    GraphicsPath& AddCurve(const std::vector<Point>& points, float tension = 0.5f) {
        if (m_path && points.size() >= 3) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_path->AddCurve(gdipPoints.data(), (INT)gdipPoints.size(), tension),
                               "GraphicsPath::AddCurve");
        }
        return *this;
    }
    
    // Shape operations
    GraphicsPath& AddEllipse(const Rect& rect) {
        if (m_path) detail::CheckStatus(m_path->AddEllipse(rect), "GraphicsPath::AddEllipse");
        return *this;
    }
    
    GraphicsPath& AddRectangle(const Rect& rect) {
        if (m_path) detail::CheckStatus(m_path->AddRectangle(rect), "GraphicsPath::AddRectangle");
        return *this;
    }
    
    GraphicsPath& AddRoundedRect(const Rect& rect, float radius) {
        if (m_path) {
            if (radius <= 0) {
                m_path->AddRectangle(rect);
            } else {
                float d = radius * 2;
                m_path->AddArc(rect.x, rect.y, d, d, 180, 90);
                m_path->AddArc(rect.Right() - d, rect.y, d, d, 270, 90);
                m_path->AddArc(rect.Right() - d, rect.Bottom() - d, d, d, 0, 90);
                m_path->AddArc(rect.x, rect.Bottom() - d, d, d, 90, 90);
                m_path->CloseFigure();
            }
        }
        return *this;
    }
    
    GraphicsPath& AddPie(const Rect& rect, float startAngle, float sweepAngle) {
        if (m_path) detail::CheckStatus(m_path->AddPie(rect, startAngle, sweepAngle), "GraphicsPath::AddPie");
        return *this;
    }
    
    GraphicsPath& AddPolygon(const std::vector<Point>& points) {
        if (m_path && !points.empty()) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_path->AddPolygon(gdipPoints.data(), (INT)gdipPoints.size()), 
                               "GraphicsPath::AddPolygon");
        }
        return *this;
    }
    
    // Text path
    GraphicsPath& AddString(const std::wstring& text, const Font& font, const Point& origin) {
        if (m_path) {
            Gdiplus::StringFormat format;
            detail::CheckStatus(m_path->AddString(text.c_str(), -1, font.Get()->GetFamily(), 
                                                  font.Get()->GetStyle(), font.Get()->GetSize(), origin, &format),
                               "GraphicsPath::AddString");
        }
        return *this;
    }
    
    // Path operations
    GraphicsPath& AddPath(const GraphicsPath& other, bool connect = false) {
        if (m_path && other.m_path) {
            detail::CheckStatus(m_path->AddPath(other.m_path.get(), connect), "GraphicsPath::AddPath");
        }
        return *this;
    }
    
    void Transform(const Gdiplus::Matrix& matrix) {
        if (m_path) detail::CheckStatus(m_path->Transform(&matrix), "GraphicsPath::Transform");
    }
    
    void Reset() {
        if (m_path) detail::CheckStatus(m_path->Reset(), "GraphicsPath::Reset");
    }
    
    void Reverse() {
        if (m_path) detail::CheckStatus(m_path->Reverse(), "GraphicsPath::Reverse");
    }
    
    // Query methods
    bool IsVisible(const Point& pt) const {
        return m_path ? m_path->IsVisible(pt) : false;
    }
    
    bool IsOutlineVisible(const Point& pt, const Pen& pen) const {
        return m_path ? m_path->IsOutlineVisible(pt, pen.Get()) : false;
    }
    
    Rect GetBounds() const {
        if (!m_path) return Rect();
        Gdiplus::RectF bounds;
        detail::CheckStatus(m_path->GetBounds(&bounds, nullptr, nullptr), "GraphicsPath::GetBounds", ErrorSeverity::Warning);
        return Rect(bounds);
    }
    
    int GetPointCount() const noexcept { return m_path ? m_path->GetPointCount() : 0; }
    
    // Flatten the path
    GraphicsPath& Flatten(const Gdiplus::Matrix* matrix = nullptr, float flatness = 0.25f) {
        if (m_path) detail::CheckStatus(m_path->Flatten(matrix, flatness), "GraphicsPath::Flatten");
        return *this;
    }
    
    // Widen the path
    GraphicsPath& Widen(const Pen& pen, const Gdiplus::Matrix* matrix = nullptr, float flatness = 0.25f) {
        if (m_path) detail::CheckStatus(m_path->Widen(pen.Get(), matrix, flatness), "GraphicsPath::Widen");
        return *this;
    }
    
    // Outline the path
    GraphicsPath& Outline(const Gdiplus::Matrix* matrix = nullptr, float flatness = 0.25f) {
        if (m_path) detail::CheckStatus(m_path->Outline(matrix, flatness), "GraphicsPath::Outline");
        return *this;
    }
};

// ============================================================================
// Region - Clipping region helper
// ============================================================================
class Region {
private:
    std::unique_ptr<Gdiplus::Region> m_region;
    
public:
    Region() : m_region(std::make_unique<Gdiplus::Region>()) {}
    explicit Region(const Rect& rect) : m_region(std::make_unique<Gdiplus::Region>(rect)) {}
    explicit Region(const GraphicsPath& path) : m_region(std::make_unique<Gdiplus::Region>(path.Get())) {}
    
    Region(const Region& other) {
        if (other.m_region) {
            m_region.reset(other.m_region->Clone());
        }
    }
    
    Region& operator=(const Region& other) {
        if (this != &other) {
            m_region.reset(other.m_region ? other.m_region->Clone() : nullptr);
        }
        return *this;
    }
    
    Region(Region&&) noexcept = default;
    Region& operator=(Region&&) noexcept = default;
    
    Gdiplus::Region* Get() const noexcept { return m_region.get(); }
    
    bool IsVisible(const Point& pt) const noexcept {
        return m_region ? m_region->IsVisible(pt) : false;
    }
    
    bool IsVisible(const Rect& rect) const noexcept {
        return m_region ? m_region->IsVisible(rect) : false;
    }
    
    Region& Intersect(const Rect& rect) {
        if (m_region) detail::CheckStatus(m_region->Intersect(rect), "Region::Intersect");
        return *this;
    }
    
    Region& Intersect(const Region& other) {
        if (m_region) detail::CheckStatus(m_region->Intersect(other.m_region.get()), "Region::Intersect");
        return *this;
    }
    
    Region& Union(const Rect& rect) {
        if (m_region) detail::CheckStatus(m_region->Union(rect), "Region::Union");
        return *this;
    }
    
    Region& Union(const Region& other) {
        if (m_region) detail::CheckStatus(m_region->Union(other.m_region.get()), "Region::Union");
        return *this;
    }
    
    Region& Exclude(const Rect& rect) {
        if (m_region) detail::CheckStatus(m_region->Exclude(rect), "Region::Exclude");
        return *this;
    }
    
    Region& Exclude(const Region& other) {
        if (m_region) detail::CheckStatus(m_region->Exclude(other.m_region.get()), "Region::Exclude");
        return *this;
    }
    
    Region& Complement(const Rect& rect) {
        if (m_region) detail::CheckStatus(m_region->Complement(rect), "Region::Complement");
        return *this;
    }
    
    Region& Translate(float dx, float dy) {
        if (m_region) detail::CheckStatus(m_region->Translate(dx, dy), "Region::Translate");
        return *this;
    }
    
    Rect GetBounds(const Graphics& g) const {
        if (!m_region) return Rect();
        Gdiplus::RectF bounds;
        detail::CheckStatus(m_region->GetBounds(&bounds, g.Get()), "Region::GetBounds", ErrorSeverity::Warning);
        return Rect(bounds);
    }
    
    bool IsEmpty(const Graphics& g) const {
        return m_region ? m_region->IsEmpty(g.Get()) : true;
    }
    
    bool IsInfinite(const Graphics& g) const {
        return m_region ? m_region->IsInfinite(g.Get()) : false;
    }
};

// ============================================================================
// Graphics - The main drawing context with state stack
// ============================================================================
class Graphics {
private:
    std::unique_ptr<Gdiplus::Graphics> m_graphics;
    bool m_owner;
    std::stack<Gdiplus::GraphicsState> m_stateStack;
    
public:
    // Create from HDC (owns the graphics context)
    Graphics(HDC hdc) 
        : m_graphics(std::make_unique<Gdiplus::Graphics>(hdc)), m_owner(true) {
        SetSmoothing(true);
        SetTextRendering(true);
    }
    
    // Wrap existing GDI+ Graphics (optionally take ownership)
    // @param g Pointer to existing Gdiplus::Graphics
    // @param takeOwnership If true, this wrapper will delete the graphics object
    Graphics(Gdiplus::Graphics* g, bool takeOwnership = false) 
        : m_graphics(std::unique_ptr<Gdiplus::Graphics>(g)), m_owner(takeOwnership) {}
    
    // Create from window handle
    explicit Graphics(HWND hwnd) 
        : m_graphics(std::make_unique<Gdiplus::Graphics>(GetDC(hwnd))), m_owner(true) {
        SetSmoothing(true);
        SetTextRendering(true);
    }
    
    // Create from Image (for drawing onto images)
    explicit Graphics(Image* image) 
        : m_graphics(std::make_unique<Gdiplus::Graphics>(image ? image->Get() : nullptr)), m_owner(true) {}
    
    ~Graphics() {
        // Restore all saved states before destruction
        while (!m_stateStack.empty()) {
            if (m_graphics) {
                m_graphics->Restore(m_stateStack.top());
            }
            m_stateStack.pop();
        }
        // Only release if we don't own the pointer (borrowed graphics)
        if (!m_owner) {
            m_graphics.release();
        }
    }
    
    // Non-copyable, but movable
    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;
    
    Graphics(Graphics&& other) noexcept 
        : m_graphics(std::move(other.m_graphics)), m_owner(other.m_owner), 
          m_stateStack(std::move(other.m_stateStack)) {
        other.m_owner = false;
    }
    
    // Quality settings
    Graphics& SetSmoothing(bool enable) noexcept {
        if (m_graphics) m_graphics->SetSmoothingMode(enable ? Gdiplus::SmoothingModeAntiAlias : Gdiplus::SmoothingModeNone);
        return *this;
    }
    
    Graphics& SetSmoothingMode(Gdiplus::SmoothingMode mode) noexcept {
        if (m_graphics) m_graphics->SetSmoothingMode(mode);
        return *this;
    }
    
    Graphics& SetTextRendering(bool highQuality) noexcept {
        if (m_graphics) m_graphics->SetTextRenderingHint(highQuality ? Gdiplus::TextRenderingHintAntiAlias : Gdiplus::TextRenderingHintSystemDefault);
        return *this;
    }
    
    Graphics& SetTextRenderingHint(Gdiplus::TextRenderingHint hint) noexcept {
        if (m_graphics) m_graphics->SetTextRenderingHint(hint);
        return *this;
    }
    
    Graphics& SetCompositingQuality(Gdiplus::CompositingQuality quality) noexcept {
        if (m_graphics) m_graphics->SetCompositingQuality(quality);
        return *this;
    }
    
    Graphics& SetCompositingMode(Gdiplus::CompositingMode mode) noexcept {
        if (m_graphics) m_graphics->SetCompositingMode(mode);
        return *this;
    }
    
    Graphics& SetInterpolationMode(Gdiplus::InterpolationMode mode) noexcept {
        if (m_graphics) m_graphics->SetInterpolationMode(mode);
        return *this;
    }
    
    Graphics& SetPixelOffsetMode(Gdiplus::PixelOffsetMode mode) noexcept {
        if (m_graphics) m_graphics->SetPixelOffsetMode(mode);
        return *this;
    }
    
    // Clear
    Graphics& Clear(const Color& color) {
        if (m_graphics) detail::CheckStatus(m_graphics->Clear(Gdiplus::Color(color.a, color.r, color.g, color.b)), "Graphics::Clear");
        return *this;
    }
    
    // Drawing primitives
    Graphics& DrawLine(const Point& p1, const Point& p2, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawLine(pen.Get(), p1, p2), "Graphics::DrawLine", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawLine(float x1, float y1, float x2, float y2, const Pen& pen) {
        return DrawLine(Point(x1, y1), Point(x2, y2), pen);
    }
    
    Graphics& DrawLines(const std::vector<Point>& points, const Pen& pen) {
        if (m_graphics && pen.IsValid() && points.size() >= 2) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_graphics->DrawLines(pen.Get(), gdipPoints.data(), (INT)gdipPoints.size()),
                               "Graphics::DrawLines", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    Graphics& DrawRect(const Rect& rect, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawRectangle(pen.Get(), rect), "Graphics::DrawRect", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& FillRect(const Rect& rect, const Brush& brush) {
        if (m_graphics && brush.IsValid()) 
            detail::CheckStatus(m_graphics->FillRectangle(brush.Get(), rect), "Graphics::FillRect", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawRoundedRect(const Rect& rect, float radius, const Pen& pen) {
        GraphicsPath path;
        path.AddRoundedRect(rect, radius);
        DrawPath(path, pen);
        return *this;
    }
    
    Graphics& FillRoundedRect(const Rect& rect, float radius, const Brush& brush) {
        GraphicsPath path;
        path.AddRoundedRect(rect, radius);
        FillPath(path, brush);
        return *this;
    }
    
    Graphics& DrawEllipse(const Rect& rect, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawEllipse(pen.Get(), rect), "Graphics::DrawEllipse", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& FillEllipse(const Rect& rect, const Brush& brush) {
        if (m_graphics && brush.IsValid()) 
            detail::CheckStatus(m_graphics->FillEllipse(brush.Get(), rect), "Graphics::FillEllipse", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawArc(const Rect& rect, float startAngle, float sweepAngle, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawArc(pen.Get(), rect, startAngle, sweepAngle), "Graphics::DrawArc", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawPie(const Rect& rect, float startAngle, float sweepAngle, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawPie(pen.Get(), rect, startAngle, sweepAngle), "Graphics::DrawPie", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& FillPie(const Rect& rect, float startAngle, float sweepAngle, const Brush& brush) {
        if (m_graphics && brush.IsValid()) 
            detail::CheckStatus(m_graphics->FillPie(brush.Get(), rect, startAngle, sweepAngle), "Graphics::FillPie", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawBezier(const Point& p1, const Point& p2, const Point& p3, const Point& p4, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawBezier(pen.Get(), p1, p2, p3, p4), "Graphics::DrawBezier", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawPolygon(const std::vector<Point>& points, const Pen& pen) {
        if (m_graphics && pen.IsValid() && !points.empty()) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_graphics->DrawPolygon(pen.Get(), gdipPoints.data(), (INT)gdipPoints.size()), 
                               "Graphics::DrawPolygon", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    Graphics& FillPolygon(const std::vector<Point>& points, const Brush& brush) {
        if (m_graphics && brush.IsValid() && !points.empty()) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_graphics->FillPolygon(brush.Get(), gdipPoints.data(), (INT)gdipPoints.size()),
                               "Graphics::FillPolygon", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    Graphics& DrawCurve(const std::vector<Point>& points, const Pen& pen, float tension = 0.5f) {
        if (m_graphics && pen.IsValid() && points.size() >= 3) {
            std::vector<Gdiplus::PointF> gdipPoints;
            gdipPoints.reserve(points.size());
            for (const auto& p : points) gdipPoints.push_back(p);
            detail::CheckStatus(m_graphics->DrawCurve(pen.Get(), gdipPoints.data(), (INT)gdipPoints.size(), tension),
                               "Graphics::DrawCurve", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    Graphics& DrawPath(GraphicsPath& path, const Pen& pen) {
        if (m_graphics && pen.IsValid()) 
            detail::CheckStatus(m_graphics->DrawPath(pen.Get(), path.Get()), "Graphics::DrawPath", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& FillPath(GraphicsPath& path, const Brush& brush) {
        if (m_graphics && brush.IsValid()) 
            detail::CheckStatus(m_graphics->FillPath(brush.Get(), path.Get()), "Graphics::FillPath", ErrorSeverity::Warning);
        return *this;
    }
    
    // Text drawing
    Graphics& DrawString(const std::wstring& text, const Font& font, const Point& pos, const Brush& brush) {
        if (m_graphics && brush.IsValid()) 
            detail::CheckStatus(m_graphics->DrawString(text.c_str(), -1, font.Get(), pos, brush.Get()), 
                               "Graphics::DrawString", ErrorSeverity::Warning);
        return *this;
    }
    
    Graphics& DrawString(const std::string& text, const Font& font, const Point& pos, const Brush& brush) {
        std::wstring wtext(text.begin(), text.end());
        return DrawString(wtext, font, pos, brush);
    }
    
    Graphics& DrawString(const std::wstring& text, const Font& font, const Rect& rect, const Brush& brush, 
                        Gdiplus::StringAlignment hAlign = Gdiplus::StringAlignmentCenter,
                        Gdiplus::StringAlignment vAlign = Gdiplus::StringAlignmentCenter) {
        if (m_graphics && brush.IsValid()) {
            Gdiplus::StringFormat format;
            format.SetAlignment(hAlign);
            format.SetLineAlignment(vAlign);
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);
            format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            detail::CheckStatus(m_graphics->DrawString(text.c_str(), -1, font.Get(), rect, &format, brush.Get()), 
                               "Graphics::DrawString", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    // Image drawing
    Graphics& DrawImage(Image& image, const Point& pos) {
        if (m_graphics && image.IsValid()) {
            detail::CheckStatus(m_graphics->DrawImage(image.Get(), pos.x, pos.y), 
                               "Graphics::DrawImage", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    Graphics& DrawImage(Image& image, const Rect& rect) {
        if (m_graphics && image.IsValid()) {
            detail::CheckStatus(m_graphics->DrawImage(image.Get(), rect), 
                               "Graphics::DrawImage", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    Graphics& DrawImage(Image& image, float x, float y, float width, float height) {
        return DrawImage(image, Rect(x, y, width, height));
    }
    
    Graphics& DrawImage(Image& image, const Rect& destRect, const Rect& srcRect, Gdiplus::Unit srcUnit = Gdiplus::UnitPixel) {
        if (m_graphics && image.IsValid()) {
            detail::CheckStatus(m_graphics->DrawImage(image.Get(), destRect, srcRect.x, srcRect.y, 
                                                       srcRect.width, srcRect.height, srcUnit),
                               "Graphics::DrawImage", ErrorSeverity::Warning);
        }
        return *this;
    }
    
    // Transformations
    Graphics& Translate(float dx, float dy) {
        if (m_graphics) detail::CheckStatus(m_graphics->TranslateTransform(dx, dy), "Graphics::Translate");
        return *this;
    }
    
    Graphics& Rotate(float angle) {
        if (m_graphics) detail::CheckStatus(m_graphics->RotateTransform(angle), "Graphics::Rotate");
        return *this;
    }
    
    Graphics& Scale(float sx, float sy) {
        if (m_graphics) detail::CheckStatus(m_graphics->ScaleTransform(sx, sy), "Graphics::Scale");
        return *this;
    }
    
    Graphics& ResetTransform() {
        if (m_graphics) detail::CheckStatus(m_graphics->ResetTransform(), "Graphics::ResetTransform");
        return *this;
    }
    
    Graphics& MultiplyTransform(const Gdiplus::Matrix& matrix, Gdiplus::MatrixOrder order = Gdiplus::MatrixOrderPrepend) {
        if (m_graphics) detail::CheckStatus(m_graphics->MultiplyTransform(&matrix, order), "Graphics::MultiplyTransform");
        return *this;
    }
    
    Graphics& SetTransform(const Gdiplus::Matrix& matrix) {
        if (m_graphics) detail::CheckStatus(m_graphics->SetTransform(&matrix), "Graphics::SetTransform");
        return *this;
    }
    
    // Clipping
    Graphics& SetClip(const Rect& rect, Gdiplus::CombineMode mode = Gdiplus::CombineModeReplace) {
        if (m_graphics) detail::CheckStatus(m_graphics->SetClip(rect, mode), "Graphics::SetClip");
        return *this;
    }
    
    Graphics& SetClip(const GraphicsPath& path, Gdiplus::CombineMode mode = Gdiplus::CombineModeReplace) {
        if (m_graphics) detail::CheckStatus(m_graphics->SetClip(path.Get(), mode), "Graphics::SetClip");
        return *this;
    }
    
    Graphics& SetClip(const Region& region, Gdiplus::CombineMode mode = Gdiplus::CombineModeReplace) {
        if (m_graphics) detail::CheckStatus(m_graphics->SetClip(region.Get(), mode), "Graphics::SetClip");
        return *this;
    }
    
    Graphics& ResetClip() {
        if (m_graphics) detail::CheckStatus(m_graphics->ResetClip(), "Graphics::ResetClip");
        return *this;
    }
    
    Rect GetClipBounds() const {
        if (!m_graphics) return Rect();
        Gdiplus::RectF bounds;
        m_graphics->GetVisibleClipBounds(&bounds);
        return Rect(bounds);
    }
    
    // Text measurement
    float MeasureString(const std::wstring& text, const Font& font) const {
        if (!m_graphics) return 0.0f;
        Gdiplus::RectF boundRect;
        Gdiplus::StringFormat format;
        format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
        m_graphics->MeasureString(text.c_str(), -1, font.Get(), Gdiplus::PointF(0, 0), &format, &boundRect);
        return boundRect.Width;
    }
    
    Gdiplus::RectF MeasureString(const std::wstring& text, const Font& font, float maxWidth) const {
        Gdiplus::RectF boundRect;
        if (!m_graphics) return boundRect;
        
        Gdiplus::RectF layoutRect(0, 0, maxWidth, 1000000);
        Gdiplus::StringFormat format;
        format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
        m_graphics->MeasureString(text.c_str(), -1, font.Get(), layoutRect, &format, &boundRect);
        return boundRect;
    }
    
    // State management with proper save/restore stack
    class StateGuard {
    private:
        Graphics* m_graphics;
        bool m_committed;
        
    public:
        explicit StateGuard(Graphics* g) : m_graphics(g), m_committed(false) {
            if (m_graphics) m_graphics->SaveState();
        }
        
        ~StateGuard() {
            if (m_graphics && !m_committed) m_graphics->RestoreState();
        }
        
        void Commit() { m_committed = true; }
        
        StateGuard(const StateGuard&) = delete;
        StateGuard& operator=(const StateGuard&) = delete;
    };
    
    void SaveState() {
        if (m_graphics) {
            Gdiplus::GraphicsState state = m_graphics->Save();
            m_stateStack.push(state);
        }
    }
    
    void RestoreState() {
        if (m_graphics && !m_stateStack.empty()) {
            Gdiplus::GraphicsState state = m_stateStack.top();
            m_stateStack.pop();
            detail::CheckStatus(m_graphics->Restore(state), "Graphics::RestoreState", ErrorSeverity::Warning);
        }
    }
    
    // Get native graphics context
    Gdiplus::Graphics* Get() const noexcept { return m_graphics.get(); }
    HDC GetHDC() { return m_graphics ? m_graphics->GetHDC() : nullptr; }
    void ReleaseHDC(HDC hdc) { if (m_graphics) m_graphics->ReleaseHDC(hdc); }
    
    // Flush
    void Flush(Gdiplus::FlushIntention intention = Gdiplus::FlushIntentionFlush) { 
        if (m_graphics) m_graphics->Flush(intention); 
    }
    
    // Visibility testing
    bool IsVisible(const Point& pt) const noexcept {
        return m_graphics ? m_graphics->IsVisible(pt) : false;
    }
    
    bool IsVisible(const Rect& rect) const noexcept {
        return m_graphics ? m_graphics->IsVisible(rect) : false;
    }
    
    // Get DPI
    float GetDpiX() const noexcept { return m_graphics ? m_graphics->GetDpiX() : 96.0f; }
    float GetDpiY() const noexcept { return m_graphics ? m_graphics->GetDpiY() : 96.0f; }
};

// ============================================================================
// Canvas - Window management with automatic double-buffering
// ============================================================================
class Canvas {
private:
    HWND m_hwnd;
    std::function<void(Graphics&)> m_paintCallback;
    static std::atomic<bool> s_classRegistered;
    static std::wstring s_className;
    static HINSTANCE s_hInstance;
    bool m_autoRedraw;
    
    static void RegisterWindowClass() {
        if (!s_classRegistered.exchange(true)) {
            s_hInstance = GetModuleHandle(nullptr);
            s_className = L"GLXCanvasClass_" + std::to_wstring(GetCurrentProcessId());
            
            WNDCLASSEX wc = {0};
            wc.cbSize = sizeof(WNDCLASSEX);
            wc.lpfnWndProc = WndProc;
            wc.hInstance = s_hInstance;
            wc.lpszClassName = s_className.c_str();
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
            RegisterClassEx(&wc);
        }
    }
    
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        Canvas* canvas = nullptr;
        
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            canvas = reinterpret_cast<Canvas*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(canvas));
            if (canvas) canvas->m_hwnd = hwnd;
        } else {
            canvas = reinterpret_cast<Canvas*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        
        if (canvas && canvas->m_paintCallback) {
            switch (msg) {
                case WM_PAINT: {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(hwnd, &ps);
                    
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    int width = rc.right - rc.left;
                    int height = rc.bottom - rc.top;
                    
                    if (width > 0 && height > 0) {
                        // Double buffering
                        HDC memDC = CreateCompatibleDC(hdc);
                        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
                        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
                        
                        // Create graphics context and render
                        {
                            Graphics g(memDC);
                            g.Clear(canvas->m_autoRedraw ? Color::White() : Color::White());
                            canvas->m_paintCallback(g);
                        }
                        
                        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
                        
                        SelectObject(memDC, oldBitmap);
                        DeleteObject(memBitmap);
                        DeleteDC(memDC);
                    }
                    
                    EndPaint(hwnd, &ps);
                    return 0;
                }
                
                case WM_ERASEBKGND:
                    return 1; // Prevent flickering
                    
                case WM_SIZE:
                    if (canvas->m_autoRedraw) {
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                    
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
            }
        }
        
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
public:
    Canvas(const std::wstring& title, int width, int height, bool autoRedraw = true) 
        : m_hwnd(nullptr), m_autoRedraw(autoRedraw) {
        RegisterWindowClass();
        
        RECT rc = {0, 0, width, height};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        
        m_hwnd = CreateWindowEx(
            0, s_className.c_str(), title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, s_hInstance, this
        );
        
        if (!m_hwnd) {
            throw GdiPlusException("Failed to create Canvas window");
        }
    }
    
    ~Canvas() {
        m_paintCallback = nullptr; // Clear callback before window destruction
        if (m_hwnd && IsWindow(m_hwnd)) {
            DestroyWindow(m_hwnd);
        }
    }
    
    // Non-copyable
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    
    // Movable
    Canvas(Canvas&& other) noexcept 
        : m_hwnd(other.m_hwnd), m_paintCallback(std::move(other.m_paintCallback)), 
          m_autoRedraw(other.m_autoRedraw) {
        other.m_hwnd = nullptr;
        if (m_hwnd) {
            SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        }
    }
    
    Canvas& OnPaint(std::function<void(Graphics&)> callback) {
        m_paintCallback = std::move(callback);
        return *this;
    }
    
    void Show(int nCmdShow = SW_SHOW) {
        ShowWindow(m_hwnd, nCmdShow);
        UpdateWindow(m_hwnd);
    }
    
    void Hide() {
        ShowWindow(m_hwnd, SW_HIDE);
    }
    
    void Redraw() {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    
    void Close() {
        SendMessage(m_hwnd, WM_CLOSE, 0, 0);
    }
    
    void SetTitle(const std::wstring& title) {
        SetWindowText(m_hwnd, title.c_str());
    }
    
    void SetAutoRedraw(bool enable) {
        m_autoRedraw = enable;
    }
    
    HWND GetHandle() const noexcept { return m_hwnd; }
    
    int GetWidth() const noexcept { 
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        return rc.right - rc.left;
    }
    
    int GetHeight() const noexcept {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        return rc.bottom - rc.top;
    }
    
    // Create graphics context for this canvas (for off-paint drawing)
    Graphics CreateGraphics() const {
        HDC hdc = GetDC(m_hwnd);
        return Graphics(hdc);
    }
    
    // Message pump that supports multiple windows
    static int RunMessageLoop() {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }
    
    // Check if any windows are still open
    static bool ProcessMessages() {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }
};

std::atomic<bool> Canvas::s_classRegistered(false);
std::wstring Canvas::s_className;
HINSTANCE Canvas::s_hInstance = nullptr;

// ============================================================================
// Utility functions
// ============================================================================
namespace detail {
    inline void SetErrorMode(ErrorMode mode) { g_errorMode = mode; }
}

// Convenience function to save any bitmap
inline bool SaveBitmap(Bitmap& bitmap, const std::wstring& filename) {
    return bitmap.Save(filename);
}

// Create a bitmap from a graphics context
inline Bitmap CaptureScreen(const Rect& rect) {
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    int width = (int)rect.width, height = (int)rect.height;
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, (int)rect.x, (int)rect.y, SRCCOPY);
    SelectObject(hdcMem, hOld);
    
    Bitmap result(width, height);
    Gdiplus::Graphics g(result.Get());
    Gdiplus::Bitmap temp(hBitmap, nullptr);
    g.DrawImage(&temp, 0, 0);
    
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    
    return result;
}

// ============================================================================
// Convenience aliases
// ============================================================================
using GdiPlus = GdiPlusManager;

} // namespace glx

#endif // GLXGDI_PLUS_H