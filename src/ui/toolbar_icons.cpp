#include "ui/toolbar_icons.h"

#include "resource.h"

// The Windows headers select the Windows 10 RTM declarations by default.
// SVG document support was added in the Windows 10 RS2 Direct2D interfaces.
#if NTDDI_VERSION < 0x0A000003
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000003
#endif

#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shlwapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <wil/resource.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace command_runner::ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr int TOOLBAR_ICON_SIZE = 16;
constexpr UINT DEFAULT_DPI = 96;
constexpr UINT TOOLBAR_RESOURCE_COUNT = 6;

constexpr std::array<WORD, TOOLBAR_RESOURCE_COUNT> TOOLBAR_RESOURCES{
    IDR_TOOLBAR_ADD,
    IDR_TOOLBAR_EDIT,
    IDR_TOOLBAR_DELETE,
    IDR_TOOLBAR_START,
    IDR_TOOLBAR_STOP,
    IDR_TOOLBAR_RESTART,
};

[[nodiscard]] DWORD errorFromHResult(HRESULT result) noexcept {
    if (HRESULT_FACILITY(result) == FACILITY_WIN32) {
        return HRESULT_CODE(result);
    }
    return ERROR_FUNCTION_FAILED;
}

[[nodiscard]] DWORD lastErrorOr(DWORD fallback) noexcept {
    const DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? fallback : error;
}

class ComApartment final {
public:
    ComApartment() : mResult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

    ~ComApartment() {
        if (SUCCEEDED(mResult)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool isUsable() const noexcept {
        return SUCCEEDED(mResult) || mResult == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT mResult{};
};

struct Direct2DRenderer final {
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<ID2D1DeviceContext5> d2dContext;
};

[[nodiscard]] std::expected<Direct2DRenderer, DWORD> createRenderer() {
    Direct2DRenderer renderer;
    D3D_FEATURE_LEVEL featureLevel{};

    constexpr UINT DEVICE_FLAGS = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT result = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       DEVICE_FLAGS,
                                       nullptr,
                                       0,
                                       D3D11_SDK_VERSION,
                                       &renderer.d3dDevice,
                                       &featureLevel,
                                       &renderer.d3dContext);
    if (FAILED(result)) {
        result = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_WARP,
                                   nullptr,
                                   DEVICE_FLAGS,
                                   nullptr,
                                   0,
                                   D3D11_SDK_VERSION,
                                   &renderer.d3dDevice,
                                   &featureLevel,
                                   &renderer.d3dContext);
    }
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    result = renderer.d3dDevice.As(&dxgiDevice);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    ComPtr<ID2D1Factory1> d2dFactory;
    result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               IID_PPV_ARGS(&d2dFactory));
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    ComPtr<ID2D1Device> d2dDevice;
    result = d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    ComPtr<ID2D1Device5> d2dDevice5;
    result = d2dDevice.As(&d2dDevice5);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    result = d2dDevice5->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &renderer.d2dContext);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    return renderer;
}

[[nodiscard]] std::expected<ComPtr<ID2D1SvgDocument>, DWORD> loadSvgDocument(
    ID2D1DeviceContext5& d2dContext,
    HINSTANCE instance,
    WORD resourceId) {
    HRSRC resource = FindResourceW(instance,
                                   MAKEINTRESOURCEW(resourceId),
                                   RT_RCDATA);
    if (resource == nullptr) {
        return std::unexpected(lastErrorOr(ERROR_RESOURCE_NAME_NOT_FOUND));
    }

    HGLOBAL loadedResource = LoadResource(instance, resource);
    if (loadedResource == nullptr) {
        return std::unexpected(lastErrorOr(ERROR_RESOURCE_DATA_NOT_FOUND));
    }

    const DWORD resourceSize = SizeofResource(instance, resource);
    if (resourceSize == 0 || resourceSize > std::numeric_limits<UINT>::max()) {
        return std::unexpected(ERROR_INVALID_DATA);
    }

    const auto* resourceData = static_cast<const BYTE*>(
        LockResource(loadedResource));
    if (resourceData == nullptr) {
        return std::unexpected(ERROR_RESOURCE_DATA_NOT_FOUND);
    }

    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(resourceData,
                                    static_cast<UINT>(resourceSize)));
    if (stream == nullptr) {
        return std::unexpected(lastErrorOr(ERROR_NOT_ENOUGH_MEMORY));
    }

    ComPtr<ID2D1SvgDocument> document;
    const HRESULT result = d2dContext.CreateSvgDocument(
        stream.Get(), D2D1::SizeF(TOOLBAR_ICON_SIZE, TOOLBAR_ICON_SIZE), &document);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }
    return document;
}

[[nodiscard]] std::expected<wil::unique_hbitmap, DWORD> renderSvg(
    Direct2DRenderer& renderer,
    HINSTANCE instance,
    WORD resourceId,
    int pixelSize) {
    auto document = loadSvgDocument(*renderer.d2dContext.Get(),
                                    instance,
                                    resourceId);
    if (!document) {
        return std::unexpected(document.error());
    }

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = static_cast<UINT>(pixelSize);
    textureDescription.Height = static_cast<UINT>(pixelSize);
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> renderTexture;
    HRESULT result = renderer.d3dDevice->CreateTexture2D(
        &textureDescription, nullptr, &renderTexture);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    ComPtr<IDXGISurface> renderSurface;
    result = renderTexture.As(&renderSurface);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    ComPtr<ID2D1Bitmap1> targetBitmap;
    result = renderer.d2dContext->CreateBitmapFromDxgiSurface(
        renderSurface.Get(), nullptr, &targetBitmap);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    renderer.d2dContext->SetTarget(targetBitmap.Get());
    const float scale = static_cast<float>(pixelSize) /
                        static_cast<float>(TOOLBAR_ICON_SIZE);
    renderer.d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    renderer.d2dContext->BeginDraw();
    renderer.d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    renderer.d2dContext->DrawSvgDocument(document->Get());
    result = renderer.d2dContext->EndDraw();
    renderer.d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    renderer.d2dContext->SetTarget(nullptr);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    D3D11_TEXTURE2D_DESC stagingDescription = textureDescription;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    result = renderer.d3dDevice->CreateTexture2D(
        &stagingDescription, nullptr, &stagingTexture);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    renderer.d3dContext->CopyResource(stagingTexture.Get(), renderTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = renderer.d3dContext->Map(stagingTexture.Get(),
                                      0,
                                      D3D11_MAP_READ,
                                      0,
                                      &mapped);
    if (FAILED(result)) {
        return std::unexpected(errorFromHResult(result));
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = pixelSize;
    bitmapInfo.bmiHeader.biHeight = -pixelSize;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bitmapPixels = nullptr;
    wil::unique_hbitmap bitmap(CreateDIBSection(nullptr,
                                                &bitmapInfo,
                                                DIB_RGB_COLORS,
                                                &bitmapPixels,
                                                nullptr,
                                                0));
    if (bitmap == nullptr || bitmapPixels == nullptr) {
        renderer.d3dContext->Unmap(stagingTexture.Get(), 0);
        return std::unexpected(lastErrorOr(ERROR_NOT_ENOUGH_MEMORY));
    }

    constexpr std::size_t BYTES_PER_PIXEL = 4;
    const std::size_t rowSize = static_cast<std::size_t>(pixelSize) *
                                BYTES_PER_PIXEL;
    auto* destination = static_cast<std::byte*>(bitmapPixels);
    const auto* source = static_cast<const std::byte*>(mapped.pData);
    for (int row = 0; row < pixelSize; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * rowSize,
                    source + static_cast<std::size_t>(row) * mapped.RowPitch,
                    rowSize);
    }
    renderer.d3dContext->Unmap(stagingTexture.Get(), 0);

    return bitmap;
}

}  // namespace

std::expected<ToolbarImageLists, DWORD> createToolbarImageLists(
    HINSTANCE instance,
    UINT dpi) {
    if (instance == nullptr) {
        return std::unexpected(ERROR_INVALID_HANDLE);
    }

    ComApartment apartment;
    if (!apartment.isUsable()) {
        return std::unexpected(ERROR_FUNCTION_FAILED);
    }

    dpi = dpi == 0 ? DEFAULT_DPI : dpi;
    const int pixelSize = MulDiv(TOOLBAR_ICON_SIZE,
                                 static_cast<int>(dpi),
                                 static_cast<int>(DEFAULT_DPI));
    if (pixelSize <= 0) {
        return std::unexpected(ERROR_INVALID_PARAMETER);
    }

    auto renderer = createRenderer();
    if (!renderer) {
        return std::unexpected(renderer.error());
    }

    ToolbarImageLists imageLists;
    imageLists.mNormalImages.Create(pixelSize,
                                    pixelSize,
                                    ILC_COLOR32 | ILC_MASK,
                                    static_cast<int>(TOOLBAR_RESOURCE_COUNT),
                                    0);
    for (const WORD resourceId : TOOLBAR_RESOURCES) {
        auto bitmap = renderSvg(*renderer,
                                instance,
                                resourceId,
                                pixelSize);
        if (!bitmap) {
            return std::unexpected(bitmap.error());
        }
        if (imageLists.mNormalImages.Add(bitmap->get()) < 0) {
            return std::unexpected(lastErrorOr(ERROR_FUNCTION_FAILED));
        }
    }

    if (!imageLists.mDisabledImages.CreateDisabledImageList(
            imageLists.mNormalImages.GetHandle())) {
        return std::unexpected(lastErrorOr(ERROR_FUNCTION_FAILED));
    }

    return imageLists;
}

}  // namespace command_runner::ui
