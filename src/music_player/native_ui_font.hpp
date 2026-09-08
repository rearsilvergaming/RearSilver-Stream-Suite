#pragma once
#include <gdiplus.h>
#include <memory>

// Initialise after GdiplusStartup; reset before GdiplusShutdown. GDI's
// AddFontResourceEx does not replace GDI+'s explicit private collection.
class NativeUiFont {
public:
    Gdiplus::Status initialise(const wchar_t *path) {
        reset();
        collection_ = std::make_unique<Gdiplus::PrivateFontCollection>();
        const auto status = collection_->AddFontFile(path);
        if (status == Gdiplus::Ok) {
            auto candidate = std::make_unique<Gdiplus::FontFamily>(L"Sora", collection_.get());
            if (usable(candidate.get())) {
                family_ = std::move(candidate);
                bundled_ = true;
                return status;
            }
        }
        // Never depend on a system-installed Sora: it masks packaging failures.
        family_ = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
        if (!usable(family_.get()))
            family_.reset(Gdiplus::FontFamily::GenericSansSerif()->Clone());
        return status;
    }
    const Gdiplus::FontFamily &family() const { return *family_; }
    bool bundled() const { return bundled_; }
    void reset() {
        family_.reset();
        collection_.reset();
        bundled_ = false;
    }
private:
    static bool usable(const Gdiplus::FontFamily *family) {
        if (!family || family->GetLastStatus() != Gdiplus::Ok) return false;
        Gdiplus::Font regular(family, 15, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font bold(family, 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        return regular.GetLastStatus() == Gdiplus::Ok && bold.GetLastStatus() == Gdiplus::Ok;
    }
    std::unique_ptr<Gdiplus::PrivateFontCollection> collection_;
    std::unique_ptr<Gdiplus::FontFamily> family_;
    bool bundled_ = false;
};
