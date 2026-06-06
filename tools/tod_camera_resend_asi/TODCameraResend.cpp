#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XXH_INLINE_ALL
#include "../../third_party/dxvk-remix/src/util/xxHash/xxhash.h"

#define TOD_ENABLE_RUNTIME_LOG 0

namespace {

constexpr DWORD kD3D9CreateDeviceIndex = 16;
constexpr DWORD kDevicePresentIndex = 17;
constexpr DWORD kDeviceCreateTextureIndex = 23;
constexpr DWORD kDeviceCreateRenderTargetIndex = 28;
constexpr DWORD kDeviceStretchRectIndex = 34;
constexpr DWORD kDeviceSetRenderTargetIndex = 37;
constexpr DWORD kDeviceSetDepthStencilSurfaceIndex = 39;
constexpr DWORD kDeviceSetTransformIndex = 44;
constexpr DWORD kDeviceSetRenderStateIndex = 57;
constexpr DWORD kDeviceSetTextureIndex = 65;
constexpr DWORD kDeviceDrawPrimitiveIndex = 81;
constexpr DWORD kDeviceDrawIndexedPrimitiveIndex = 82;
constexpr DWORD kDeviceDrawPrimitiveUPIndex = 83;
constexpr DWORD kDeviceDrawIndexedPrimitiveUPIndex = 84;
constexpr DWORD kDeviceCreateVertexDeclarationIndex = 86;
constexpr DWORD kDeviceSetVertexDeclarationIndex = 87;
constexpr DWORD kDeviceSetFVFIndex = 89;
constexpr DWORD kDeviceSetVertexShaderIndex = 92;

constexpr bool kRedirectFullSizeSceneRtToPrimary = true;
constexpr bool kSkipRtTextureCompositesToPrimary = true;
constexpr bool kRedirectAllColorRtTexturesToPrimary = true;
constexpr bool kSkipAllRtTextureComposites = true;
constexpr bool kPreloadManagedTexturesBeforeBind = false;
constexpr bool kBindDefaultCopiesForManagedTextures = true;
constexpr bool kCopyCompressedManagedTextures = true;
constexpr bool kCopyWhitelistedUncompressedManagedTextures = true;
constexpr bool kBindPreTransformedA8CopiesAfterWorld = true;
constexpr bool kBindManagedA8DrawCopies = true;
constexpr bool kPatchTodCameraFarClip = true;
constexpr bool kMarkTodSkyDrawsWithViewportMinZ = true;
constexpr bool kEnableTodSkyTextureDiscovery = true;
constexpr bool kHookTodSkyBoxRenderTextureScope = true;
// Diagnostic: log TOD's fixed-function fog render-state to tod-fog-state.tsv so
// Remix fog config can be set from evidence (does TOD set FOGENABLE/FOGCOLOR,
// with what color, on sky vs world draws) instead of guessed.
constexpr bool kEnableTodFogStateLog = false;
constexpr bool kEnableTodSkyAssetLoadProbe = false;
constexpr bool kEnableTodTextureMapProbe = false;
// Inject a D3D9 directional light each frame so Remix ray-traces a real sun. TOD has
// no sun light (lighting is baked into vertex colors; the on-screen sun is only a 2D
// billboard). Remix's SetLight picks up any ENABLED light (DirtyLights) regardless of
// D3DRS_LIGHTING, and TOD ignores it because its fixed-function lighting is off.
// Direction/color are runtime-tunable from scripts/TODCameraResend.ini while we
// match the light to TOD's visible sky/lens-flare sun.
// DISABLED 2026-06-06: TOD's sky/skybox already casts a correct single sun shadow via
// Remix's environment probe. Injecting a D3D9 directional light added a SECOND shadow
// that pointed a different way than the skybox sun - and that held true even after the
// auto-aim subsystem below aimed the injected light perfectly at TOD's lens-flare sun
// (the flare and the skybox sun are at different directions). So the injected sun was
// redundant and produced the "two opposite shadows". Leaving it off lets the skybox
// probe be the sole sun. The injection + auto-aim machinery is retained (gated off) in
// case a crisp directional sun is wanted later, which would also require suppressing the
// skybox probe's shadow so the two don't double up.
constexpr bool kInjectSunLight = false;
constexpr DWORD kSunLightIndex = 0;
// Auto-aim the injected sun. TOD's "sun" is a LensFlare emitter placed at a world
// position; it renders a camera-facing billboard at that position. When that sprite
// draws we read its world-space center, derive direction-to-sun, and aim the injected
// directional light opposite it (light travels from the sun toward the scene). This
// matches the injected sun to TOD's actual sky sun per mission automatically instead of
// a hand-tuned vector, so the ray-traced shadow lines up with the visible sun (kills the
// "two opposite shadows" - injected light vs sky/env IBL - by making them agree). The
// sprite is identified by its Remix texture hash from scripts/TODCameraResend.ini
// ([Sun] SpriteHashHex - config, never hardcoded in the binary). The last valid
// direction is latched so it persists while the sun is off-screen; until the sun is
// first seen the injected light uses the INI Direction* fallback.
// Off while the injected sun (kInjectSunLight) is disabled - auto-aim only feeds that
// light. Re-enable both together if a crisp directional sun is wanted later.
constexpr bool kEnableSunAutoAim = false;
constexpr LONG kSunHashAttemptsPerFrame = 3;
// Camera-resend workaround. DIAGNOSTIC 2026-06-04: disabling it made the displaced-
// geometry glitch WORSE (geometry more distorted), confirming the resend STABILIZES
// the camera and is not the glitch's cause. Kept enabled.
constexpr bool kEnableCameraResend = true;
// DIAGNOSTIC: capture the index-buffer layout of alpha quad batches. CONFIRMED TOD
// triangulates quads as A,B,C,C,D,A while Remix's createBillboards needs A,B,C,A,C,D.
// (tod-billboard-indices.tsv: ABCACD=0). Verified remap works (post-remap ABCACD=1,
// remapDraws=113, lockFails=0) but it did NOT fix the displaced-foliage glitch, so the
// billboard index layout was not the cause. Capture off (diagnostic complete).
constexpr bool kCaptureBillboardIndices = false;
// Remaps TOD's quad index layout A,B,C,C,D,A -> A,B,C,A,C,D so Remix's createBillboards
// accepts the quads. VERIFIED working but did NOT fix the displaced-foliage glitch, and
// it adds per-draw IB locking + risks mis-billboarding real card meshes - so disabled
// to keep a clean state. Code retained; re-enable if billboard handling is wanted.
constexpr bool kRemapBillboardIndices = false;
// TEST: TOD CPU-animates/sways alpha-tested foliage by rewriting vertex positions.
// Remix hashes vertex positions for generation and rejects removing that component,
// so these meshes lose temporal identity and flicker. For large fixed-function
// alpha world draws, bind a compact frozen copy of the draw's vertex range so Remix
// sees stable geometry. The original stream source is restored immediately after
// the draw. This intentionally freezes foliage sway; it is a proof/mitigation, not
// a broad content hook.
constexpr bool kFreezeAnimatedAlphaWorldVertexBuffers = false;
constexpr UINT kFreezeAlphaWorldMinPrimitiveCount = 80;
constexpr UINT kFreezeAlphaWorldMinVertexCount = 64;
constexpr UINT kFreezeAlphaWorldMaxCopyBytes = 512u * 1024u;
constexpr LONG kMaxFrozenAlphaWorldVertexBufferRecords = 512;
constexpr float kTodCameraFarClipOverride = 8000.0f;  // render distance (was 3000); sane cap is 10000
constexpr float kTodSkyViewportMinZ = 0.999f;
constexpr uintptr_t kTodImageBaseVa = 0x00400000u;
constexpr uintptr_t kTodLoadNativeResourceVa = 0x00878AB0u;
constexpr uintptr_t kTodLoadNativeResourceRva = kTodLoadNativeResourceVa - kTodImageBaseVa;
constexpr uintptr_t kTodTextureDrawAllTexturesVa = 0x00463850u;
constexpr uintptr_t kTodTextureDrawAllTexturesRva = kTodTextureDrawAllTexturesVa - kTodImageBaseVa;
constexpr uintptr_t kTodTextureAssetAllocatorGlobalVa = 0x00A3BE18u;
constexpr uintptr_t kTodTextureAssetAllocatorGlobalRva =
    kTodTextureAssetAllocatorGlobalVa - kTodImageBaseVa;
constexpr uintptr_t kTodTextureAssetLoadFlagGlobalVa = 0x00A3BE2Du;
constexpr uintptr_t kTodTextureAssetLoadFlagGlobalRva =
    kTodTextureAssetLoadFlagGlobalVa - kTodImageBaseVa;
constexpr uintptr_t kTodCameraSystemGlobalVa = 0x00A3DCBCu;
constexpr uintptr_t kTodCameraSystemGlobalRva = kTodCameraSystemGlobalVa - kTodImageBaseVa;
constexpr uintptr_t kTodSkyMeshArrayGlobalVa = 0x00A3E0B0u;
constexpr uintptr_t kTodSkyMeshArrayGlobalRva = kTodSkyMeshArrayGlobalVa - kTodImageBaseVa;
constexpr uintptr_t kTodSkyBoxRenderVa = 0x008F1E10u;
constexpr uintptr_t kTodSkyBoxRenderRva = kTodSkyBoxRenderVa - kTodImageBaseVa;
constexpr uintptr_t kTodRenderListSetMaterialVa = 0x00431660u;
constexpr uintptr_t kTodRenderListSetMaterialRva =
    kTodRenderListSetMaterialVa - kTodImageBaseVa;
constexpr uintptr_t kTodRenderListAddMeshVa = 0x00432C70u;
constexpr uintptr_t kTodRenderListAddMeshRva =
    kTodRenderListAddMeshVa - kTodImageBaseVa;
constexpr uintptr_t kTodRenderMeshDrawVa = 0x004540E0u;
constexpr uintptr_t kTodRenderMeshDrawRva =
    kTodRenderMeshDrawVa - kTodImageBaseVa;
constexpr uintptr_t kTodCameraSlotPrimaryOffset = 0x60u;
constexpr uintptr_t kTodCameraSlotSecondaryOffset = 0x64u;
constexpr uintptr_t kTodCameraSlotCurrentOffset = 0x6Cu;
constexpr uintptr_t kTodCameraNearClipOffset = 0xB8u;
constexpr uintptr_t kTodCameraFarClipOffset = 0xBCu;
constexpr uintptr_t kTodRenderMeshVertexBufferOffset = 0x0Cu;
constexpr uintptr_t kTodRenderMeshIndexBufferOffset = 0x10u;
constexpr uintptr_t kTodVertexBufferD3DOffset = 0x24u;
constexpr uintptr_t kTodIndexBufferD3DOffset = 0x1Cu;
constexpr int kTodSkyMeshCount = 5;
constexpr LONG kMaxTodSkyRenderMeshes = 64;
constexpr LONG kMaxTodSkyTextureRecords = 128;
constexpr LONG kTodTextureMapProbeFrames = 5;
constexpr LONG kMaxTodForcedTextureRecords = 8192;
constexpr LONG kMaxTodForcedTexturePending = 2048;
constexpr LONG kTodTextureAssetAllocatorId = 6;
constexpr SIZE_T kInlineHookPatchBytes = 6;
constexpr SIZE_T kTodRenderListSetMaterialPatchBytes = 7;
constexpr SIZE_T kTodRenderListAddMeshPatchBytes = 7;
constexpr SIZE_T kTodRenderMeshDrawPatchBytes = 9;

constexpr DWORD kDdsdCaps = 0x00000001u;
constexpr DWORD kDdsdHeight = 0x00000002u;
constexpr DWORD kDdsdWidth = 0x00000004u;
constexpr DWORD kDdsdPitch = 0x00000008u;
constexpr DWORD kDdsdPixelFormat = 0x00001000u;
constexpr DWORD kDdsdLinearSize = 0x00080000u;
constexpr DWORD kDdpfAlphaPixels = 0x00000001u;
constexpr DWORD kDdpfAlpha = 0x00000002u;
constexpr DWORD kDdpfFourCc = 0x00000004u;
constexpr DWORD kDdpfRgb = 0x00000040u;
constexpr DWORD kDdpfLuminance = 0x00020000u;
constexpr DWORD kDdsCapsTexture = 0x00001000u;

using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(APIENTRY*)(
    IDirect3D9* self,
    UINT adapter,
    D3DDEVTYPE deviceType,
    HWND focusWindow,
    DWORD behaviorFlags,
    D3DPRESENT_PARAMETERS* presentationParameters,
    IDirect3DDevice9** returnedDevice);
using PresentFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    const RECT* sourceRect,
    const RECT* destRect,
    HWND destWindowOverride,
    const RGNDATA* dirtyRegion);
using SetTransformFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    D3DTRANSFORMSTATETYPE state,
    const D3DMATRIX* matrix);
using SetRenderStateFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    D3DRENDERSTATETYPE state,
    DWORD value);
using CreateTextureFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    UINT width,
    UINT height,
    UINT levels,
    DWORD usage,
    D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DTexture9** texture,
    HANDLE* sharedHandle);
using CreateRenderTargetFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    UINT width,
    UINT height,
    D3DFORMAT format,
    D3DMULTISAMPLE_TYPE multiSample,
    DWORD multisampleQuality,
    BOOL lockable,
    IDirect3DSurface9** surface,
    HANDLE* sharedHandle);
using StretchRectFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    IDirect3DSurface9* sourceSurface,
    const RECT* sourceRect,
    IDirect3DSurface9* destSurface,
    const RECT* destRect,
    D3DTEXTUREFILTERTYPE filter);
using SetRenderTargetFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    DWORD renderTargetIndex,
    IDirect3DSurface9* surface);
using SetDepthStencilSurfaceFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    IDirect3DSurface9* surface);
using SetTextureFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    DWORD stage,
    IDirect3DBaseTexture9* texture);
using DrawPrimitiveFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    UINT startVertex,
    UINT primitiveCount);
using DrawIndexedPrimitiveFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    UINT startIndex,
    UINT primitiveCount);
using DrawPrimitiveUPFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    UINT primitiveCount,
    const void* vertexStreamZeroData,
    UINT vertexStreamZeroStride);
using DrawIndexedPrimitiveUPFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    UINT minVertexIndex,
    UINT numVertices,
    UINT primitiveCount,
    const void* indexData,
    D3DFORMAT indexDataFormat,
    const void* vertexStreamZeroData,
    UINT vertexStreamZeroStride);
using CreateVertexDeclarationFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    const D3DVERTEXELEMENT9* vertexElements,
    IDirect3DVertexDeclaration9** declaration);
using SetVertexDeclarationFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    IDirect3DVertexDeclaration9* declaration);
using SetFVFFn = HRESULT(APIENTRY*)(IDirect3DDevice9* self, DWORD fvf);
using SetVertexShaderFn = HRESULT(APIENTRY*)(
    IDirect3DDevice9* self,
    IDirect3DVertexShader9* shader);
using TodSkyBoxRenderFn = void (__fastcall*)(void* self, void* edx);
using TodRenderListSetMaterialFn =
    void (__fastcall*)(void* self, void* edx, void* material, DWORD stage);
using TodRenderListAddMeshFn = void (__fastcall*)(void* self, void* edx, void* mesh);
using TodRenderMeshDrawFn = void (__fastcall*)(void* self, void* edx, void* mesh);
using TodLoadNativeResourceFn = void* (__cdecl*)(char* resourcePath);
using TodTextureDrawAllTexturesFn = void (__cdecl*)();

struct DeclInfo {
  IDirect3DVertexDeclaration9* declaration;
  bool hasPosition;
  bool hasPositionT;
};

struct LayoutState {
  DWORD fvf;
  IDirect3DVertexDeclaration9* declaration;
  IDirect3DVertexShader9* vertexShader;
  bool fvfRhw;
  bool declarationPositionT;
  bool shaderActive;
  bool unknown;
};

struct SurfaceInfo {
  IDirect3DSurface9* surface;
  IDirect3DBaseTexture9* ownerTexture;
  UINT width;
  UINT height;
  D3DFORMAT format;
  DWORD usage;
  D3DPOOL pool;
  bool fromTexture;
};

struct TextureInfo {
  IDirect3DBaseTexture9* texture;
  IDirect3DSurface9* level0Surface;
  UINT width;
  UINT height;
  UINT levels;
  D3DFORMAT format;
  DWORD usage;
  D3DPOOL pool;
};

struct TextureCopyInfo {
  IDirect3DBaseTexture9* source;
  IDirect3DBaseTexture9* replacement;
  bool attempted;
};

struct FrozenAlphaWorldVertexBufferRecord {
  bool valid;
  IDirect3DVertexBuffer9* original;
  IDirect3DIndexBuffer9* indexBuffer;
  IDirect3DBaseTexture9* texture;
  IDirect3DVertexBuffer9* frozen;
  UINT streamOffset;
  UINT stride;
  UINT firstVertex;
  UINT numVertices;
  UINT startIndex;
  UINT primitiveCount;
  UINT copyBytes;
};

struct FrozenAlphaWorldBind {
  IDirect3DVertexBuffer9* original;
  UINT streamOffset;
  UINT stride;
  INT drawBaseVertexIndex;
  bool applied;
};

struct DdsPixelFormat {
  DWORD size;
  DWORD flags;
  DWORD fourCc;
  DWORD rgbBitCount;
  DWORD rBitMask;
  DWORD gBitMask;
  DWORD bBitMask;
  DWORD aBitMask;
};

struct DdsHeader {
  DWORD size;
  DWORD flags;
  DWORD height;
  DWORD width;
  DWORD pitchOrLinearSize;
  DWORD depth;
  DWORD mipMapCount;
  DWORD reserved1[11];
  DdsPixelFormat pixelFormat;
  DWORD caps;
  DWORD caps2;
  DWORD caps3;
  DWORD caps4;
  DWORD reserved2;
};

struct TodSkyTextureRecord {
  IDirect3DBaseTexture9* texture;
  unsigned long long hash;
  bool valid;
  bool drawnLogged;
};

struct TodSkyTexturePending {
  IDirect3DBaseTexture9* sourceTexture;
  IDirect3DBaseTexture9* boundTexture;
  LONG skyDraws;
  UINT primitiveCount;
  UINT numVertices;
};

struct TodForcedTextureRecord {
  IDirect3DBaseTexture9* texture;
  unsigned long long hash;
  bool valid;
};

struct TodForcedTexturePending {
  IDirect3DBaseTexture9* sourceTexture;
  IDirect3DBaseTexture9* boundTexture;
  LONG probeFrame;
};

struct SunLightConfig {
  float directionX;
  float directionY;
  float directionZ;
  float diffuseR;
  float diffuseG;
  float diffuseB;
  float specularScale;
  float range;
};

char g_gameRoot[MAX_PATH * 2] = {};
char g_logPath[MAX_PATH * 2] = {};
char g_sunConfigPath[MAX_PATH * 2] = {};
char g_todSunAutoAimLogPath[MAX_PATH * 2] = {};
char g_todFogStateLogPath[MAX_PATH * 2] = {};
char g_todBillboardLogPath[MAX_PATH * 2] = {};
char g_todFoliageFreezeLogPath[MAX_PATH * 2] = {};
char g_todSkyTextureLogPath[MAX_PATH * 2] = {};
char g_todSkyTextureDumpDir[MAX_PATH * 2] = {};
char g_todSkyAssetLoadProbeLogPath[MAX_PATH * 2] = {};
char g_todForcedTextureProbeLogPath[MAX_PATH * 2] = {};
char g_todForcedTextureProbeDumpDir[MAX_PATH * 2] = {};

CreateDeviceFn g_origCreateDevice = nullptr;
PresentFn g_origPresent = nullptr;
CreateTextureFn g_origCreateTexture = nullptr;
CreateRenderTargetFn g_origCreateRenderTarget = nullptr;
StretchRectFn g_origStretchRect = nullptr;
SetRenderTargetFn g_origSetRenderTarget = nullptr;
SetDepthStencilSurfaceFn g_origSetDepthStencilSurface = nullptr;
SetTransformFn g_origSetTransform = nullptr;
SetRenderStateFn g_origSetRenderState = nullptr;
SetTextureFn g_origSetTexture = nullptr;
DrawPrimitiveFn g_origDrawPrimitive = nullptr;
DrawIndexedPrimitiveFn g_origDrawIndexedPrimitive = nullptr;
DrawPrimitiveUPFn g_origDrawPrimitiveUP = nullptr;
DrawIndexedPrimitiveUPFn g_origDrawIndexedPrimitiveUP = nullptr;
CreateVertexDeclarationFn g_origCreateVertexDeclaration = nullptr;
SetVertexDeclarationFn g_origSetVertexDeclaration = nullptr;
SetFVFFn g_origSetFVF = nullptr;
SetVertexShaderFn g_origSetVertexShader = nullptr;

D3DMATRIX g_lastView = {};
D3DMATRIX g_lastProjection = {};
DWORD g_currentFvf = 0;
IDirect3DVertexDeclaration9* g_currentDeclaration = nullptr;
IDirect3DVertexShader9* g_currentVertexShader = nullptr;
IDirect3DSurface9* g_currentRenderTarget0 = nullptr;
IDirect3DSurface9* g_currentDepthStencil = nullptr;
IDirect3DBaseTexture9* g_currentTexture0 = nullptr;
IDirect3DBaseTexture9* g_currentSourceTexture0 = nullptr;
IDirect3DSurface9* g_primaryRenderTarget = nullptr;
DeclInfo g_declInfo[512] = {};
SurfaceInfo g_surfaceInfo[2048] = {};
TextureInfo g_textureInfo[16384] = {};
TextureCopyInfo g_textureCopyInfo[8192] = {};
FrozenAlphaWorldVertexBufferRecord
    g_frozenAlphaWorldVertexBuffers[kMaxFrozenAlphaWorldVertexBufferRecords] = {};
TodSkyTextureRecord g_todSkyTextureRecords[kMaxTodSkyTextureRecords] = {};
TodSkyTexturePending g_todSkyTexturePending[32] = {};
TodForcedTextureRecord g_todForcedTextureRecords[kMaxTodForcedTextureRecords] = {};
TodForcedTexturePending g_todForcedTexturePending[kMaxTodForcedTexturePending] = {};
SunLightConfig g_sunConfig = {
    0.0f,
    -0.8f,
    -0.6f,
    2.4f,
    2.15f,
    1.8f,
    1.0f,
    100000.0f,
};
DWORD g_sunConfigLastReadTick = 0;

// Sun auto-aim state (see kEnableSunAutoAim). spriteHash/enabled come from the INI;
// the rest is captured at runtime when the sun billboard draws.
volatile LONG g_sunAutoAimEnabled = 1;
unsigned long long g_sunSpriteHash = 0;  // [Sun] SpriteHashHex; 0 = disabled until set
IDirect3DBaseTexture9* g_sunSpriteTexture = nullptr;  // resolved sun sprite pointer
volatile LONG g_sunAutoDirValid = 0;
float g_sunAutoDirX = 0.0f;
float g_sunAutoDirY = -0.8f;
float g_sunAutoDirZ = -0.6f;
volatile LONG g_sunAutoCaptures = 0;
volatile LONG g_sunHashAttemptsThisFrame = 0;
volatile LONG g_sunCheckedCount = 0;
IDirect3DBaseTexture9* g_sunCheckedPtrs[512] = {};
// WORLD transform cache (the sun billboard's vertices are object-space; apply this to
// get world space). Most TOD billboards are CPU-built with an identity world, but we
// honor a set world matrix if TOD provides one.
D3DMATRIX g_lastWorld = {};
volatile LONG g_haveWorld = 0;

TodSkyBoxRenderFn g_origTodSkyBoxRender = nullptr;
TodRenderListSetMaterialFn g_origTodRenderListSetMaterial = nullptr;
TodRenderListAddMeshFn g_origTodRenderListAddMesh = nullptr;
TodRenderMeshDrawFn g_origTodRenderMeshDraw = nullptr;
BYTE g_todSkyBoxRenderOriginalBytes[kInlineHookPatchBytes] = {};
BYTE g_todRenderListSetMaterialOriginalBytes[kTodRenderListSetMaterialPatchBytes] = {};
BYTE g_todRenderListAddMeshOriginalBytes[kTodRenderListAddMeshPatchBytes] = {};
BYTE g_todRenderMeshDrawOriginalBytes[kTodRenderMeshDrawPatchBytes] = {};
uintptr_t g_todSkyRenderMeshes[kMaxTodSkyRenderMeshes] = {};

volatile LONG g_declInfoCount = 0;
volatile LONG g_surfaceInfoCount = 0;
volatile LONG g_textureInfoCount = 0;
volatile LONG g_textureCopyInfoCount = 0;
volatile LONG g_haveView = 0;
volatile LONG g_haveProjection = 0;
volatile LONG g_currentViewIsIdentity = 0;
volatile LONG g_ignoredIdentityViews = 0;
volatile LONG g_identityViewResendSkips = 0;
volatile LONG g_resendDepth = 0;
volatile LONG g_logLock = 0;
volatile LONG g_deviceHooked = 0;
volatile LONG g_createDeviceCalls = 0;
volatile LONG g_setViewCalls = 0;
volatile LONG g_setProjectionCalls = 0;
volatile LONG g_setRenderStateCalls = 0;
volatile LONG g_setFvfCalls = 0;
volatile LONG g_createTextureCalls = 0;
volatile LONG g_createRenderTargetCalls = 0;
volatile LONG g_setRenderTargetCalls = 0;
volatile LONG g_setDepthStencilCalls = 0;
volatile LONG g_setTextureCalls = 0;
volatile LONG g_preloadTextureCalls = 0;
volatile LONG g_textureCopyAttempts = 0;
volatile LONG g_textureCopySuccesses = 0;
volatile LONG g_textureCopyFailures = 0;
volatile LONG g_textureCopyBinds = 0;
volatile LONG g_frozenAlphaWorldCandidates = 0;
volatile LONG g_frozenAlphaWorldRecordCount = 0;
volatile LONG g_frozenAlphaWorldCreates = 0;
volatile LONG g_frozenAlphaWorldBinds = 0;
volatile LONG g_frozenAlphaWorldCreateFails = 0;
volatile LONG g_frozenAlphaWorldLockFails = 0;
volatile LONG g_frozenAlphaWorldRows = 0;
volatile LONG g_stretchRectCalls = 0;
volatile LONG g_redirectedRenderTargetCalls = 0;
volatile LONG g_skippedRtCompositeDraws = 0;
volatile LONG g_setDeclarationCalls = 0;
volatile LONG g_createDeclarationCalls = 0;
volatile LONG g_setVertexShaderCalls = 0;
volatile LONG g_presentCalls = 0;
volatile LONG g_sunLightInjections = 0;
volatile LONG g_indexedDrawCalls = 0;
volatile LONG g_primitiveDrawCalls = 0;
volatile LONG g_indexedUpDrawCalls = 0;
volatile LONG g_primitiveUpDrawCalls = 0;
volatile LONG g_resendCalls = 0;
volatile LONG g_fixedFunctionIndexedDraws = 0;
volatile LONG g_preTransformedIndexedDraws = 0;
volatile LONG g_shaderIndexedDraws = 0;
volatile LONG g_unknownIndexedDraws = 0;
volatile LONG g_preTransformedPrimitiveDraws = 0;
volatile LONG g_preTransformedUpDraws = 0;
volatile LONG g_worldDrawnThisFrame = 0;
volatile LONG g_preTransformedA8CopyBinds = 0;
volatile LONG g_managedA8DrawCopyBinds = 0;
volatile LONG g_zEnable = 1;
volatile LONG g_zWriteEnable = 1;
volatile LONG g_alphaBlendEnable = 0;
volatile LONG g_alphaTestEnable = 0;
volatile LONG g_cameraFarClipPatches = 0;
volatile LONG g_todSkyDraws = 0;
volatile LONG g_todSkyViewportMarks = 0;
volatile LONG g_todSkyTextureRows = 0;
volatile LONG g_todSkyTextureRecordCount = 0;
volatile LONG g_todSkyTexturePendingCount = 0;
volatile LONG g_todSkyBoxRenderHooked = 0;
volatile LONG g_todRenderListSetMaterialHooked = 0;
volatile LONG g_todRenderListAddMeshHooked = 0;
volatile LONG g_todRenderMeshDrawHooked = 0;
volatile LONG g_todSkyBoxRenderDepth = 0;
volatile LONG g_todSkyMeshDrawDepth = 0;
volatile LONG g_todSkyBoxRenderCalls = 0;
volatile LONG g_todSkyBoxRenderFirstCallLogged = 0;
volatile LONG g_todSkyMaterialCommands = 0;
volatile LONG g_todSkyMeshCommands = 0;
volatile LONG g_todSkyMeshRecords = 0;
volatile LONG g_todSkyMeshDrawExecutions = 0;
volatile LONG g_todSkyMeshDrawFirstLogged = 0;
volatile LONG g_todSkyViewportFirstLogged = 0;
volatile LONG g_todSkyScopedTextureQueues = 0;
volatile LONG g_todForcedTextureRows = 0;
volatile LONG g_todForcedTextureRecordCount = 0;
volatile LONG g_todForcedTexturePendingCount = 0;
volatile LONG g_todTextureMapProbeFramesDone = 0;
volatile LONG g_todTextureMapProbeActive = 0;
volatile LONG g_todSkyAssetLoadProbeDone = 0;
volatile LONG g_todSkyAssetLoadProbeRows = 0;

void StripFileName(char* path) {
  char* slash = strrchr(path, '\\');
  if (slash != nullptr) {
    *slash = '\0';
  }
}

void InitializePaths(HMODULE module) {
  char modulePath[MAX_PATH * 2] = {};
  if (GetModuleFileNameA(module, modulePath, static_cast<DWORD>(sizeof(modulePath))) == 0) {
    GetCurrentDirectoryA(static_cast<DWORD>(sizeof(g_gameRoot)), g_gameRoot);
  } else {
    StripFileName(modulePath);
    strcpy_s(g_gameRoot, modulePath);

    char* lastSlash = strrchr(g_gameRoot, '\\');
    if (lastSlash != nullptr && _stricmp(lastSlash + 1, "scripts") == 0) {
      *lastSlash = '\0';
    }
  }

  snprintf(
      g_logPath,
      sizeof(g_logPath),
      "%s\\rtx-remix\\logs\\tod-camera-resend.log",
      g_gameRoot);
  snprintf(
      g_sunConfigPath,
      sizeof(g_sunConfigPath),
      "%s\\scripts\\TODCameraResend.ini",
      g_gameRoot);
  snprintf(
      g_todSunAutoAimLogPath,
      sizeof(g_todSunAutoAimLogPath),
      "%s\\rtx-remix\\logs\\tod-sun-autoaim.log",
      g_gameRoot);
  snprintf(
      g_todFogStateLogPath,
      sizeof(g_todFogStateLogPath),
      "%s\\rtx-remix\\logs\\tod-fog-state.tsv",
      g_gameRoot);
  snprintf(
      g_todBillboardLogPath,
      sizeof(g_todBillboardLogPath),
      "%s\\rtx-remix\\logs\\tod-billboard-indices.tsv",
      g_gameRoot);
  snprintf(
      g_todFoliageFreezeLogPath,
      sizeof(g_todFoliageFreezeLogPath),
      "%s\\rtx-remix\\logs\\tod-foliage-freeze.tsv",
      g_gameRoot);
  snprintf(
      g_todSkyTextureLogPath,
      sizeof(g_todSkyTextureLogPath),
      "%s\\rtx-remix\\logs\\tod-sky-textures.tsv",
      g_gameRoot);
  snprintf(
      g_todSkyTextureDumpDir,
      sizeof(g_todSkyTextureDumpDir),
      "%s\\rtx-remix\\logs\\tod-sky-textures",
      g_gameRoot);
  snprintf(
      g_todSkyAssetLoadProbeLogPath,
      sizeof(g_todSkyAssetLoadProbeLogPath),
      "%s\\rtx-remix\\logs\\tod-sky-asset-load-probe.tsv",
      g_gameRoot);
  snprintf(
      g_todForcedTextureProbeLogPath,
      sizeof(g_todForcedTextureProbeLogPath),
      "%s\\rtx-remix\\logs\\tod-forced-texture-probe.tsv",
      g_gameRoot);
  snprintf(
      g_todForcedTextureProbeDumpDir,
      sizeof(g_todForcedTextureProbeDumpDir),
      "%s\\rtx-remix\\logs\\tod-forced-texture-probe",
      g_gameRoot);
}

void LockLog() {
  while (InterlockedCompareExchange(const_cast<LONG*>(&g_logLock), 1, 0) != 0) {
    Sleep(0);
  }
}

void UnlockLog() {
  InterlockedExchange(const_cast<LONG*>(&g_logLock), 0);
}

void EnsureLogDirectory() {
  char remixDir[MAX_PATH * 2] = {};
  char logsDir[MAX_PATH * 2] = {};
  snprintf(remixDir, sizeof(remixDir), "%s\\rtx-remix", g_gameRoot);
  snprintf(logsDir, sizeof(logsDir), "%s\\rtx-remix\\logs", g_gameRoot);
  CreateDirectoryA(remixDir, nullptr);
  CreateDirectoryA(logsDir, nullptr);
  if (g_todSkyTextureDumpDir[0] != '\0') {
    CreateDirectoryA(g_todSkyTextureDumpDir, nullptr);
  }
  if (g_todForcedTextureProbeDumpDir[0] != '\0') {
    CreateDirectoryA(g_todForcedTextureProbeDumpDir, nullptr);
  }
}

void Log(const char* fmt, ...) {
  char msg[1024] = {};

  va_list args;
  va_start(args, fmt);
  vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
  va_end(args);

  SYSTEMTIME st = {};
  GetLocalTime(&st);

  char line[1280] = {};
  const int count = snprintf(
      line,
      sizeof(line),
      "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
      st.wYear,
      st.wMonth,
      st.wDay,
      st.wHour,
      st.wMinute,
      st.wSecond,
      st.wMilliseconds,
      msg);

  if (count <= 0) {
    return;
  }

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      g_logPath[0] != '\0' ? g_logPath : "rtx-remix\\logs\\tod-camera-resend.log",
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

#if !TOD_ENABLE_RUNTIME_LOG
#pragma warning(disable : 4100 4189)
#define Log(...) ((void)0)
#endif

bool IsCommittedMemoryRange(uintptr_t address, size_t bytes, bool requireWritable) {
  if (address == 0 || bytes == 0 || address + bytes < address) {
    return false;
  }

  MEMORY_BASIC_INFORMATION mbi = {};
  if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) {
    return false;
  }

  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
    return false;
  }

  const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
  const uintptr_t regionEnd = regionBase + mbi.RegionSize;
  if (address < regionBase || address + bytes > regionEnd || regionEnd < regionBase) {
    return false;
  }

  const DWORD protect = mbi.Protect & 0xFFu;
  if (!requireWritable) {
    return protect == PAGE_READONLY || protect == PAGE_READWRITE ||
        protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
        protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
  }

  return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
      protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

bool TryReadPointer(uintptr_t address, uintptr_t* value) {
  if (value == nullptr || !IsCommittedMemoryRange(address, sizeof(uintptr_t), false)) {
    return false;
  }

  *value = *reinterpret_cast<const uintptr_t*>(address);
  return *value != 0;
}

bool TryReadFloat(uintptr_t address, float* value) {
  if (value == nullptr || !IsCommittedMemoryRange(address, sizeof(float), false)) {
    return false;
  }

  *value = *reinterpret_cast<const float*>(address);
  return true;
}

uintptr_t StripTodPointerFlags(uintptr_t value) {
  return value & ~static_cast<uintptr_t>(3);
}

uintptr_t TodModuleBase() {
  HMODULE exeModule = GetModuleHandleA(nullptr);
  return exeModule != nullptr ? reinterpret_cast<uintptr_t>(exeModule) : 0;
}

const char* const kTodSkyTextureAssetPaths[] = {
    "/data/Textures/Skybox/ClearBlueSky/ClearBlueSky/skybox_BK.bmp",
    "/data/Textures/Skybox/ClearBlueSky/ClearBlueSky/skybox_FR.bmp",
    "/data/Textures/Skybox/ClearBlueSky/ClearBlueSky/skybox_LF.bmp",
    "/data/Textures/Skybox/ClearBlueSky/ClearBlueSky/skybox_RT.bmp",
    "/data/Textures/Skybox/ClearBlueSky/ClearBlueSky/skybox_UP.bmp",
    "/data/Textures/Skybox/RedDustSky/RedDustSky/skybox_BK.bmp",
    "/data/Textures/Skybox/RedDustSky/RedDustSky/skybox_FR.bmp",
    "/data/Textures/Skybox/RedDustSky/RedDustSky/skybox_LF.bmp",
    "/data/Textures/Skybox/RedDustSky/RedDustSky/skybox_RT.bmp",
    "/data/Textures/Skybox/RedDustSky/RedDustSky/skybox_UP.bmp",
    "/data/Textures/Skybox/Sky/cloudSky/skybox_BK.bmp",
    "/data/Textures/Skybox/Sky/cloudSky/skybox_FR.bmp",
    "/data/Textures/Skybox/Sky/cloudSky/skybox_LF.bmp",
    "/data/Textures/Skybox/Sky/cloudSky/skybox_RT.bmp",
    "/data/Textures/Skybox/Sky/cloudSky/skybox_UP.bmp",
    "/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_BK.bmp",
    "/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_FR.bmp",
    "/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_LF.bmp",
    "/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_RT.bmp",
    "/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_UP.bmp",
    "/data/Textures/Skybox/Skybox_DAY_OF_THE_DEAD/Skybox_DAY_OF_THE_DEAD/skybox_BK.bmp",
    "/data/Textures/Skybox/Skybox_DAY_OF_THE_DEAD/Skybox_DAY_OF_THE_DEAD/skybox_FR.bmp",
    "/data/Textures/Skybox/Skybox_DAY_OF_THE_DEAD/Skybox_DAY_OF_THE_DEAD/skybox_LF.bmp",
    "/data/Textures/Skybox/Skybox_DAY_OF_THE_DEAD/Skybox_DAY_OF_THE_DEAD/skybox_RT.bmp",
    "/data/Textures/Skybox/Skybox_DAY_OF_THE_DEAD/Skybox_DAY_OF_THE_DEAD/skybox_UP.bmp",
    "/data/Textures/Skybox/Skybox_NIGHT/Skybox_NIGHT/skybox_BK.bmp",
    "/data/Textures/Skybox/Skybox_NIGHT/Skybox_NIGHT/skybox_FR.bmp",
    "/data/Textures/Skybox/Skybox_NIGHT/Skybox_NIGHT/skybox_LF.bmp",
    "/data/Textures/Skybox/Skybox_NIGHT/Skybox_NIGHT/skybox_RT.bmp",
    "/data/Textures/Skybox/Skybox_NIGHT/Skybox_NIGHT/skybox_UP.bmp",
    "/data/textures/materials/map06_virgillosmap_test/SkyHorison _Light.bmp",
};

bool TryGetTodSkyMeshBuffers(
    int index,
    IDirect3DVertexBuffer9** vertexBuffer,
    IDirect3DIndexBuffer9** indexBuffer) {
  if (vertexBuffer == nullptr || indexBuffer == nullptr || index < 0 || index >= kTodSkyMeshCount) {
    return false;
  }

  *vertexBuffer = nullptr;
  *indexBuffer = nullptr;

  const uintptr_t moduleBase = TodModuleBase();
  if (moduleBase == 0) {
    return false;
  }

  uintptr_t mesh = 0;
  const uintptr_t meshSlot = moduleBase + kTodSkyMeshArrayGlobalRva + sizeof(uintptr_t) * index;
  if (!TryReadPointer(meshSlot, &mesh)) {
    return false;
  }

  uintptr_t vertexBufferWrapper = 0;
  uintptr_t indexBufferWrapper = 0;
  if (!TryReadPointer(mesh + kTodRenderMeshVertexBufferOffset, &vertexBufferWrapper) ||
      !TryReadPointer(mesh + kTodRenderMeshIndexBufferOffset, &indexBufferWrapper)) {
    return false;
  }

  vertexBufferWrapper = StripTodPointerFlags(vertexBufferWrapper);
  indexBufferWrapper = StripTodPointerFlags(indexBufferWrapper);
  if (vertexBufferWrapper == 0 || indexBufferWrapper == 0) {
    return false;
  }

  uintptr_t vertexBufferPtr = 0;
  uintptr_t indexBufferPtr = 0;
  if (!TryReadPointer(vertexBufferWrapper + kTodVertexBufferD3DOffset, &vertexBufferPtr) ||
      !TryReadPointer(indexBufferWrapper + kTodIndexBufferD3DOffset, &indexBufferPtr)) {
    return false;
  }

  vertexBufferPtr = StripTodPointerFlags(vertexBufferPtr);
  indexBufferPtr = StripTodPointerFlags(indexBufferPtr);
  if (vertexBufferPtr == 0 || indexBufferPtr == 0) {
    return false;
  }

  *vertexBuffer = reinterpret_cast<IDirect3DVertexBuffer9*>(vertexBufferPtr);
  *indexBuffer = reinterpret_cast<IDirect3DIndexBuffer9*>(indexBufferPtr);
  return true;
}

bool CurrentDrawUsesTodSkyMesh(IDirect3DDevice9* device, const LayoutState& layout) {
  if ((!kMarkTodSkyDrawsWithViewportMinZ && !kEnableTodSkyTextureDiscovery) || device == nullptr) {
    return false;
  }

  if (layout.fvfRhw || layout.declarationPositionT || layout.shaderActive) {
    return false;
  }

  IDirect3DVertexBuffer9* currentVertexBuffer = nullptr;
  UINT streamOffset = 0;
  UINT streamStride = 0;
  if (FAILED(device->GetStreamSource(0, &currentVertexBuffer, &streamOffset, &streamStride)) ||
      currentVertexBuffer == nullptr) {
    return false;
  }

  IDirect3DIndexBuffer9* currentIndexBuffer = nullptr;
  if (FAILED(device->GetIndices(&currentIndexBuffer)) || currentIndexBuffer == nullptr) {
    currentVertexBuffer->Release();
    return false;
  }

  bool matched = false;
  for (int i = 0; i < kTodSkyMeshCount; ++i) {
    IDirect3DVertexBuffer9* skyVertexBuffer = nullptr;
    IDirect3DIndexBuffer9* skyIndexBuffer = nullptr;
    if (TryGetTodSkyMeshBuffers(i, &skyVertexBuffer, &skyIndexBuffer) &&
        currentVertexBuffer == skyVertexBuffer &&
        currentIndexBuffer == skyIndexBuffer) {
      matched = true;
      break;
    }
  }

  currentIndexBuffer->Release();
  currentVertexBuffer->Release();
  return matched;
}

bool ApplyTodSkyViewportMarker(IDirect3DDevice9* device, D3DVIEWPORT9* savedViewport) {
  if (!kMarkTodSkyDrawsWithViewportMinZ || device == nullptr || savedViewport == nullptr) {
    return false;
  }

  if (FAILED(device->GetViewport(savedViewport))) {
    return false;
  }

  D3DVIEWPORT9 skyViewport = *savedViewport;
  skyViewport.MinZ = kTodSkyViewportMinZ;
  skyViewport.MaxZ = 1.0f;
  if (FAILED(device->SetViewport(&skyViewport))) {
    return false;
  }

  InterlockedIncrement(const_cast<LONG*>(&g_todSkyViewportMarks));
  return true;
}

bool IsFiniteFloat(float value) {
  return value == value && value > -3.402823e38f && value < 3.402823e38f;
}

float ReadIniFloat(const char* section, const char* key, float defaultValue) {
  char valueText[64] = {};
  GetPrivateProfileStringA(section, key, "", valueText, sizeof(valueText), g_sunConfigPath);
  if (valueText[0] == '\0') {
    return defaultValue;
  }

  char* end = nullptr;
  const double parsed = strtod(valueText, &end);
  if (end == valueText || !IsFiniteFloat(static_cast<float>(parsed))) {
    return defaultValue;
  }
  return static_cast<float>(parsed);
}

unsigned long long ReadIniHashU64(
    const char* section,
    const char* key,
    unsigned long long defaultValue) {
  char valueText[64] = {};
  GetPrivateProfileStringA(section, key, "", valueText, sizeof(valueText), g_sunConfigPath);
  if (valueText[0] == '\0') {
    return defaultValue;
  }
  char* end = nullptr;
  // _strtoui64 with base 16 accepts an optional 0x prefix.
  const unsigned long long parsed = _strtoui64(valueText, &end, 16);
  if (end == valueText) {
    return defaultValue;
  }
  return parsed;
}

void NormalizeSunDirection(SunLightConfig* config) {
  const float lengthSq =
      config->directionX * config->directionX +
      config->directionY * config->directionY +
      config->directionZ * config->directionZ;
  if (!IsFiniteFloat(lengthSq) || lengthSq < 0.0001f) {
    config->directionX = 0.0f;
    config->directionY = -0.8f;
    config->directionZ = -0.6f;
    return;
  }

  const float invLength = 1.0f / sqrtf(lengthSq);
  config->directionX *= invLength;
  config->directionY *= invLength;
  config->directionZ *= invLength;
}

void RefreshSunLightConfig() {
  const DWORD now = GetTickCount();
  if (g_sunConfigLastReadTick != 0 && now - g_sunConfigLastReadTick < 1000) {
    return;
  }
  g_sunConfigLastReadTick = now;

  SunLightConfig next = g_sunConfig;
  next.directionX = ReadIniFloat("Sun", "DirectionX", next.directionX);
  next.directionY = ReadIniFloat("Sun", "DirectionY", next.directionY);
  next.directionZ = ReadIniFloat("Sun", "DirectionZ", next.directionZ);
  next.diffuseR = ReadIniFloat("Sun", "DiffuseR", next.diffuseR);
  next.diffuseG = ReadIniFloat("Sun", "DiffuseG", next.diffuseG);
  next.diffuseB = ReadIniFloat("Sun", "DiffuseB", next.diffuseB);
  next.specularScale = ReadIniFloat("Sun", "SpecularScale", next.specularScale);
  next.range = ReadIniFloat("Sun", "Range", next.range);
  NormalizeSunDirection(&next);

  g_sunConfig = next;

  // Auto-aim controls (config only; the sun sprite hash never lives in the binary).
  InterlockedExchange(
      const_cast<LONG*>(&g_sunAutoAimEnabled),
      GetPrivateProfileIntA("Sun", "AutoAim", 1, g_sunConfigPath) != 0 ? 1 : 0);
  g_sunSpriteHash = ReadIniHashU64("Sun", "SpriteHashHex", g_sunSpriteHash);
}

bool IsSaneTodCameraClipPair(float nearClip, float farClip) {
  return IsFiniteFloat(nearClip) && IsFiniteFloat(farClip) &&
      nearClip >= 0.01f && nearClip <= 10.0f &&
      farClip >= 10.0f && farClip <= 10000.0f &&
      farClip > nearClip;
}

bool PatchTodCameraFarClip(uintptr_t camera) {
  if (!kPatchTodCameraFarClip || camera == 0) {
    return false;
  }

  const uintptr_t nearClipAddress = camera + kTodCameraNearClipOffset;
  const uintptr_t farClipAddress = camera + kTodCameraFarClipOffset;
  if (nearClipAddress < camera || farClipAddress < camera ||
      !IsCommittedMemoryRange(farClipAddress, sizeof(float), true)) {
    return false;
  }

  float nearClip = 0.0f;
  float farClip = 0.0f;
  if (!TryReadFloat(nearClipAddress, &nearClip) || !TryReadFloat(farClipAddress, &farClip) ||
      !IsSaneTodCameraClipPair(nearClip, farClip)) {
    return false;
  }

  if (farClip >= kTodCameraFarClipOverride - 1.0f) {
    return false;
  }

  *reinterpret_cast<float*>(farClipAddress) = kTodCameraFarClipOverride;
  const LONG patches = InterlockedIncrement(const_cast<LONG*>(&g_cameraFarClipPatches));
  if (patches <= 10 || (patches % 300) == 0) {
    Log(
        "TOD camera farclip override #%ld: camera=%p near=%.3f oldFar=%.3f newFar=%.3f",
        patches,
        reinterpret_cast<void*>(camera),
        nearClip,
        farClip,
        kTodCameraFarClipOverride);
  }
  return true;
}

void PatchTodCameraSlot(uintptr_t cameraSystem, uintptr_t slotOffset, uintptr_t* patchedCameraA, uintptr_t* patchedCameraB) {
  uintptr_t camera = 0;
  if (!TryReadPointer(cameraSystem + slotOffset, &camera)) {
    return;
  }

  if ((patchedCameraA != nullptr && camera == *patchedCameraA) ||
      (patchedCameraB != nullptr && camera == *patchedCameraB)) {
    return;
  }

  if (PatchTodCameraFarClip(camera)) {
    if (patchedCameraA != nullptr && *patchedCameraA == 0) {
      *patchedCameraA = camera;
    } else if (patchedCameraB != nullptr && *patchedCameraB == 0) {
      *patchedCameraB = camera;
    }
  }
}

void ApplyTodCameraFarClipOverride() {
  if (!kPatchTodCameraFarClip) {
    return;
  }

  HMODULE exeModule = GetModuleHandleA(nullptr);
  if (exeModule == nullptr) {
    return;
  }

  const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(exeModule);
  const uintptr_t cameraSystemGlobal = moduleBase + kTodCameraSystemGlobalRva;
  uintptr_t cameraSystem = 0;
  if (!TryReadPointer(cameraSystemGlobal, &cameraSystem)) {
    return;
  }

  uintptr_t patchedCameraA = 0;
  uintptr_t patchedCameraB = 0;
  PatchTodCameraSlot(cameraSystem, kTodCameraSlotCurrentOffset, &patchedCameraA, &patchedCameraB);
  PatchTodCameraSlot(cameraSystem, kTodCameraSlotPrimaryOffset, &patchedCameraA, &patchedCameraB);
  PatchTodCameraSlot(cameraSystem, kTodCameraSlotSecondaryOffset, &patchedCameraA, &patchedCameraB);
}

bool NearlyEqual(float a, float b) {
  const float delta = a - b;
  return delta > -0.0001f && delta < 0.0001f;
}

bool IsIdentityView(const D3DMATRIX& matrix) {
  return NearlyEqual(matrix._11, 1.0f) && NearlyEqual(matrix._12, 0.0f) &&
      NearlyEqual(matrix._13, 0.0f) && NearlyEqual(matrix._14, 0.0f) &&
      NearlyEqual(matrix._21, 0.0f) && NearlyEqual(matrix._22, 1.0f) &&
      NearlyEqual(matrix._23, 0.0f) && NearlyEqual(matrix._24, 0.0f) &&
      NearlyEqual(matrix._31, 0.0f) && NearlyEqual(matrix._32, 0.0f) &&
      NearlyEqual(matrix._33, 1.0f) && NearlyEqual(matrix._34, 0.0f) &&
      NearlyEqual(matrix._41, 0.0f) && NearlyEqual(matrix._42, 0.0f) &&
      NearlyEqual(matrix._43, 0.0f) && NearlyEqual(matrix._44, 1.0f);
}

bool IsPreTransformedFvf(DWORD fvf) {
  return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
}

const char* FormatName(D3DFORMAT format) {
  switch (format) {
    case D3DFMT_UNKNOWN:
      return "UNKNOWN";
    case D3DFMT_R5G6B5:
      return "R5G6B5";
    case D3DFMT_X1R5G5B5:
      return "X1R5G5B5";
    case D3DFMT_A1R5G5B5:
      return "A1R5G5B5";
    case D3DFMT_X8R8G8B8:
      return "X8R8G8B8";
    case D3DFMT_A8R8G8B8:
      return "A8R8G8B8";
    case D3DFMT_A8:
      return "A8";
    case D3DFMT_L8:
      return "L8";
    case D3DFMT_A8L8:
      return "A8L8";
    case D3DFMT_DXT1:
      return "DXT1";
    case D3DFMT_DXT3:
      return "DXT3";
    case D3DFMT_DXT5:
      return "DXT5";
    case D3DFMT_D16:
      return "D16";
    case D3DFMT_D24S8:
      return "D24S8";
    case D3DFMT_D24X8:
      return "D24X8";
    default:
      return "OTHER";
  }
}

bool IsRenderTargetUsage(DWORD usage) {
  return (usage & D3DUSAGE_RENDERTARGET) != 0;
}

bool IsManagedRegularTexture(const TextureInfo* info) {
  return info != nullptr && info->pool == D3DPOOL_MANAGED &&
      !IsRenderTargetUsage(info->usage);
}

bool IsCompressedTextureFormat(D3DFORMAT format) {
  return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 ||
      format == D3DFMT_DXT3 || format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
}

bool IsWhitelistedUncompressedCopyCandidate(const TextureInfo* info) {
  if (info == nullptr || info->format != D3DFMT_A8R8G8B8) {
    return false;
  }

  // TOD uses 640x360 managed A8R8G8B8 textures for video/cutscene frames.
  // Copying those once into DEFAULT freezes/churns video, so leave them alone.
  if (info->width == 640 && info->height == 360) {
    return false;
  }

  // The broad A8 copy path fixed HUD/text but also broke later missions. Keep
  // this to likely UI/font/HUD atlases and one-mip menu textures.
  if (info->levels == 1) {
    if ((info->width == 1024 && info->height == 128) ||
        (info->width == 512 && info->height == 512) ||
        (info->width == 256 && info->height == 256) ||
        (info->width == 128 && info->height == 128) ||
        (info->width == 64 && info->height == 64) ||
        (info->width == 8 && info->height == 8)) {
      return true;
    }

    if (info->width <= 32 && info->height <= 32) {
      return true;
    }
  }

  return false;
}

bool ShouldCreateDefaultTextureCopy(const TextureInfo* info) {
  if (!IsManagedRegularTexture(info)) {
    return false;
  }

  if (kCopyCompressedManagedTextures && IsCompressedTextureFormat(info->format)) {
    return true;
  }

  if (kCopyWhitelistedUncompressedManagedTextures &&
      IsWhitelistedUncompressedCopyCandidate(info)) {
    return true;
  }

  return false;
}

UINT MinUInt(UINT a, UINT b) {
  return a < b ? a : b;
}

int MinInt(int a, int b) {
  return a < b ? a : b;
}

bool ComputeTextureCopyLayout(D3DFORMAT format, UINT width, UINT height, UINT* rowBytes, UINT* rowCount) {
  if (rowBytes == nullptr || rowCount == nullptr) {
    return false;
  }

  switch (format) {
    case D3DFMT_DXT1:
      *rowBytes = ((width + 3) / 4) * 8;
      *rowCount = (height + 3) / 4;
      break;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
      *rowBytes = ((width + 3) / 4) * 16;
      *rowCount = (height + 3) / 4;
      break;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
      *rowBytes = width * 4;
      *rowCount = height;
      break;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A8L8:
      *rowBytes = width * 2;
      *rowCount = height;
      break;
    case D3DFMT_A8:
    case D3DFMT_L8:
      *rowBytes = width;
      *rowCount = height;
      break;
    default:
      return false;
  }

  if (*rowBytes == 0) {
    *rowBytes = 1;
  }
  if (*rowCount == 0) {
    *rowCount = 1;
  }
  return true;
}

bool IsPrimarySurfaceInfo(const SurfaceInfo* info) {
  return info != nullptr && !info->fromTexture;
}

bool IsFullSizeSceneRenderTarget(const SurfaceInfo* info) {
  return info != nullptr && info->fromTexture && info->width >= 2000 &&
      info->height >= 1000 && info->format == D3DFMT_A8R8G8B8;
}

bool IsColorRenderTargetTextureSurface(const SurfaceInfo* info) {
  return info != nullptr && info->fromTexture && IsRenderTargetUsage(info->usage) &&
      info->format == D3DFMT_A8R8G8B8;
}

LONG ClampedCount(volatile LONG* value, LONG maxCount) {
  LONG count = InterlockedCompareExchange(const_cast<LONG*>(value), 0, 0);
  if (count > maxCount) {
    count = maxCount;
  }
  return count;
}

const SurfaceInfo* FindSurfaceInfo(IDirect3DSurface9* surface) {
  if (surface == nullptr) {
    return nullptr;
  }

  const LONG maxCount = static_cast<LONG>(sizeof(g_surfaceInfo) / sizeof(g_surfaceInfo[0]));
  const LONG count = ClampedCount(&g_surfaceInfoCount, maxCount);
  for (LONG i = 0; i < count; ++i) {
    if (g_surfaceInfo[i].surface == surface) {
      return &g_surfaceInfo[i];
    }
  }

  return nullptr;
}

SurfaceInfo* RegisterSurface(
    IDirect3DSurface9* surface,
    IDirect3DBaseTexture9* ownerTexture,
    UINT width,
    UINT height,
    D3DFORMAT format,
    DWORD usage,
    D3DPOOL pool,
    bool fromTexture) {
  if (surface == nullptr) {
    return nullptr;
  }

  const LONG maxCount = static_cast<LONG>(sizeof(g_surfaceInfo) / sizeof(g_surfaceInfo[0]));
  LONG count = ClampedCount(&g_surfaceInfoCount, maxCount);
  for (LONG i = 0; i < count; ++i) {
    if (g_surfaceInfo[i].surface == surface) {
      g_surfaceInfo[i].ownerTexture = ownerTexture;
      g_surfaceInfo[i].width = width;
      g_surfaceInfo[i].height = height;
      g_surfaceInfo[i].format = format;
      g_surfaceInfo[i].usage = usage;
      g_surfaceInfo[i].pool = pool;
      g_surfaceInfo[i].fromTexture = fromTexture;
      return &g_surfaceInfo[i];
    }
  }

  if (count >= maxCount) {
    return nullptr;
  }

  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_surfaceInfoCount)) - 1;
  if (index < 0 || index >= maxCount) {
    return nullptr;
  }

  g_surfaceInfo[index].surface = surface;
  g_surfaceInfo[index].ownerTexture = ownerTexture;
  g_surfaceInfo[index].width = width;
  g_surfaceInfo[index].height = height;
  g_surfaceInfo[index].format = format;
  g_surfaceInfo[index].usage = usage;
  g_surfaceInfo[index].pool = pool;
  g_surfaceInfo[index].fromTexture = fromTexture;
  return &g_surfaceInfo[index];
}

SurfaceInfo* EnsureSurfaceInfo(IDirect3DSurface9* surface, bool fromTexture) {
  const SurfaceInfo* existing = FindSurfaceInfo(surface);
  if (existing != nullptr) {
    return const_cast<SurfaceInfo*>(existing);
  }

  if (surface == nullptr) {
    return nullptr;
  }

  D3DSURFACE_DESC desc = {};
  if (FAILED(surface->GetDesc(&desc))) {
    return nullptr;
  }

  return RegisterSurface(
      surface,
      nullptr,
      desc.Width,
      desc.Height,
      desc.Format,
      desc.Usage,
      desc.Pool,
      fromTexture);
}

const TextureInfo* FindTextureInfo(IDirect3DBaseTexture9* texture) {
  if (texture == nullptr) {
    return nullptr;
  }

  const LONG maxCount = static_cast<LONG>(sizeof(g_textureInfo) / sizeof(g_textureInfo[0]));
  const LONG count = ClampedCount(&g_textureInfoCount, maxCount);
  for (LONG i = 0; i < count; ++i) {
    if (g_textureInfo[i].texture == texture) {
      return &g_textureInfo[i];
    }
  }

  return nullptr;
}

DWORD MakeFourCc(char a, char b, char c, char d) {
  return static_cast<DWORD>(static_cast<BYTE>(a)) |
      (static_cast<DWORD>(static_cast<BYTE>(b)) << 8) |
      (static_cast<DWORD>(static_cast<BYTE>(c)) << 16) |
      (static_cast<DWORD>(static_cast<BYTE>(d)) << 24);
}

bool ReadTextureLevel0Packed(
    IDirect3DBaseTexture9* texture,
    const TextureInfo* info,
    BYTE** packedData,
    SIZE_T* packedSize,
    UINT* outRowBytes,
    UINT* outRowCount) {
  if (texture == nullptr || info == nullptr || packedData == nullptr || packedSize == nullptr ||
      texture->GetType() != D3DRTYPE_TEXTURE) {
    return false;
  }

  UINT rowBytes = 0;
  UINT rowCount = 0;
  if (!ComputeTextureCopyLayout(info->format, info->width, info->height, &rowBytes, &rowCount)) {
    return false;
  }

  IDirect3DTexture9* texture2D = static_cast<IDirect3DTexture9*>(texture);
  D3DLOCKED_RECT locked = {};
  const HRESULT lockResult = texture2D->LockRect(0, &locked, nullptr, D3DLOCK_READONLY);
  if (FAILED(lockResult) || locked.pBits == nullptr) {
    return false;
  }

  if (locked.Pitch <= 0 || static_cast<UINT>(locked.Pitch) < rowBytes) {
    texture2D->UnlockRect(0);
    return false;
  }

  const UINT packedRowBytes = (rowBytes + 3u) & ~3u;
  const SIZE_T levelDataSize = static_cast<SIZE_T>(packedRowBytes) * rowCount;
  if (levelDataSize == 0 || levelDataSize > (64u * 1024u * 1024u)) {
    texture2D->UnlockRect(0);
    return false;
  }

  BYTE* packed = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, levelDataSize));
  if (packed == nullptr) {
    texture2D->UnlockRect(0);
    return false;
  }

  const BYTE* base = static_cast<const BYTE*>(locked.pBits);
  for (UINT y = 0; y < rowCount; ++y) {
    const BYTE* row = base + static_cast<int>(y) * locked.Pitch;
    CopyMemory(packed + static_cast<SIZE_T>(y) * packedRowBytes, row, rowBytes);
  }

  texture2D->UnlockRect(0);
  *packedData = packed;
  *packedSize = levelDataSize;
  if (outRowBytes != nullptr) {
    *outRowBytes = rowBytes;
  }
  if (outRowCount != nullptr) {
    *outRowCount = rowCount;
  }
  return true;
}

bool ComputeRtxTextureHash(
    IDirect3DBaseTexture9* texture,
    const TextureInfo* info,
    unsigned long long* rtxHash) {
  if (rtxHash == nullptr) {
    return false;
  }

  BYTE* packed = nullptr;
  SIZE_T packedSize = 0;
  if (!ReadTextureLevel0Packed(texture, info, &packed, &packedSize, nullptr, nullptr)) {
    return false;
  }

  *rtxHash = XXH3_64bits(packed, packedSize);
  HeapFree(GetProcessHeap(), 0, packed);
  return true;
}

bool FillDdsHeader(const TextureInfo* info, UINT rowBytes, UINT rowCount, DdsHeader* header) {
  if (info == nullptr || header == nullptr) {
    return false;
  }

  *header = {};
  header->size = 124;
  header->flags = kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFormat;
  header->height = info->height;
  header->width = info->width;
  header->pixelFormat.size = 32;
  header->caps = kDdsCapsTexture;

  switch (info->format) {
    case D3DFMT_DXT1:
      header->flags |= kDdsdLinearSize;
      header->pitchOrLinearSize = rowBytes * rowCount;
      header->pixelFormat.flags = kDdpfFourCc;
      header->pixelFormat.fourCc = MakeFourCc('D', 'X', 'T', '1');
      return true;
    case D3DFMT_DXT3:
      header->flags |= kDdsdLinearSize;
      header->pitchOrLinearSize = rowBytes * rowCount;
      header->pixelFormat.flags = kDdpfFourCc;
      header->pixelFormat.fourCc = MakeFourCc('D', 'X', 'T', '3');
      return true;
    case D3DFMT_DXT5:
      header->flags |= kDdsdLinearSize;
      header->pitchOrLinearSize = rowBytes * rowCount;
      header->pixelFormat.flags = kDdpfFourCc;
      header->pixelFormat.fourCc = MakeFourCc('D', 'X', 'T', '5');
      return true;
    case D3DFMT_A8R8G8B8:
      header->flags |= kDdsdPitch;
      header->pitchOrLinearSize = rowBytes;
      header->pixelFormat.flags = kDdpfRgb | kDdpfAlphaPixels;
      header->pixelFormat.rgbBitCount = 32;
      header->pixelFormat.rBitMask = 0x00FF0000u;
      header->pixelFormat.gBitMask = 0x0000FF00u;
      header->pixelFormat.bBitMask = 0x000000FFu;
      header->pixelFormat.aBitMask = 0xFF000000u;
      return true;
    case D3DFMT_X8R8G8B8:
      header->flags |= kDdsdPitch;
      header->pitchOrLinearSize = rowBytes;
      header->pixelFormat.flags = kDdpfRgb;
      header->pixelFormat.rgbBitCount = 32;
      header->pixelFormat.rBitMask = 0x00FF0000u;
      header->pixelFormat.gBitMask = 0x0000FF00u;
      header->pixelFormat.bBitMask = 0x000000FFu;
      return true;
    case D3DFMT_A8:
      header->flags |= kDdsdPitch;
      header->pitchOrLinearSize = rowBytes;
      header->pixelFormat.flags = kDdpfAlpha;
      header->pixelFormat.rgbBitCount = 8;
      header->pixelFormat.aBitMask = 0x000000FFu;
      return true;
    case D3DFMT_L8:
      header->flags |= kDdsdPitch;
      header->pitchOrLinearSize = rowBytes;
      header->pixelFormat.flags = kDdpfLuminance;
      header->pixelFormat.rgbBitCount = 8;
      header->pixelFormat.rBitMask = 0x000000FFu;
      return true;
    case D3DFMT_A8L8:
      header->flags |= kDdsdPitch;
      header->pitchOrLinearSize = rowBytes;
      header->pixelFormat.flags = kDdpfLuminance | kDdpfAlphaPixels;
      header->pixelFormat.rgbBitCount = 16;
      header->pixelFormat.rBitMask = 0x000000FFu;
      header->pixelFormat.aBitMask = 0x0000FF00u;
      return true;
    default:
      return false;
  }
}

void DumpTextureDds(
    IDirect3DBaseTexture9* texture,
    const TextureInfo* info,
    unsigned long long rtxHash,
    const char* filePrefix,
    const char* dumpDir,
    const char* fallbackDumpDir) {
  if (texture == nullptr || info == nullptr || filePrefix == nullptr) {
    return;
  }

  BYTE* packed = nullptr;
  SIZE_T packedSize = 0;
  UINT rowBytes = 0;
  UINT rowCount = 0;
  if (!ReadTextureLevel0Packed(texture, info, &packed, &packedSize, &rowBytes, &rowCount)) {
    return;
  }

  DdsHeader header = {};
  if (!FillDdsHeader(info, rowBytes, rowCount, &header)) {
    HeapFree(GetProcessHeap(), 0, packed);
    return;
  }

  char path[MAX_PATH * 3] = {};
  snprintf(
      path,
      sizeof(path),
      "%s\\%s_%016llX_%ux%u_%s.dds",
      dumpDir != nullptr && dumpDir[0] != '\0' ? dumpDir : fallbackDumpDir,
      filePrefix,
      rtxHash,
      info->width,
      info->height,
      FormatName(info->format));

  if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
    HeapFree(GetProcessHeap(), 0, packed);
    return;
  }

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      path,
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    const DWORD magic = MakeFourCc('D', 'D', 'S', ' ');
    WriteFile(file, &magic, sizeof(magic), &written, nullptr);
    WriteFile(file, &header, sizeof(header), &written, nullptr);
    WriteFile(file, packed, static_cast<DWORD>(packedSize), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();

  HeapFree(GetProcessHeap(), 0, packed);
}

void DumpTodSkyTextureDds(
    IDirect3DBaseTexture9* texture,
    const TextureInfo* info,
    unsigned long long rtxHash) {
  DumpTextureDds(
      texture,
      info,
      rtxHash,
      "skybox",
      g_todSkyTextureDumpDir,
      "rtx-remix\\logs\\tod-sky-textures");
}

void DumpTodForcedTextureProbeDds(
    IDirect3DBaseTexture9* texture,
    const TextureInfo* info,
    unsigned long long rtxHash) {
  DumpTextureDds(
      texture,
      info,
      rtxHash,
      "texture",
      g_todForcedTextureProbeDumpDir,
      "rtx-remix\\logs\\tod-forced-texture-probe");
}

TodSkyTextureRecord* FindTodSkyTextureRecord(
    IDirect3DBaseTexture9* texture,
    unsigned long long hash,
    bool valid) {
  const LONG count = ClampedCount(&g_todSkyTextureRecordCount, kMaxTodSkyTextureRecords);
  for (LONG i = 0; i < count; ++i) {
    TodSkyTextureRecord& record = g_todSkyTextureRecords[i];
    if (record.texture == texture) {
      return &record;
    }
    if (valid && record.valid && record.hash == hash) {
      return &record;
    }
  }
  return nullptr;
}

bool TodSkyTextureRecordExists(
    IDirect3DBaseTexture9* texture,
    unsigned long long hash,
    bool valid) {
  return FindTodSkyTextureRecord(texture, hash, valid) != nullptr;
}

// Whether the texture (by pointer only, the identity available at queue time)
// has already produced a sky_draw_marked row. Used to dedup draw entries
// without blocking them behind the earlier material-bind record.
bool TodSkyTextureKeyDrawnLogged(IDirect3DBaseTexture9* textureKey) {
  if (textureKey == nullptr) {
    return false;
  }
  const LONG count = ClampedCount(&g_todSkyTextureRecordCount, kMaxTodSkyTextureRecords);
  for (LONG i = 0; i < count; ++i) {
    if (g_todSkyTextureRecords[i].texture == textureKey) {
      return g_todSkyTextureRecords[i].drawnLogged;
    }
  }
  return false;
}

void AddTodSkyTextureRecord(
    IDirect3DBaseTexture9* texture,
    unsigned long long hash,
    bool valid,
    bool drawnLogged) {
  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_todSkyTextureRecordCount)) - 1;
  if (index < 0 || index >= kMaxTodSkyTextureRecords) {
    return;
  }
  g_todSkyTextureRecords[index].texture = texture;
  g_todSkyTextureRecords[index].hash = hash;
  g_todSkyTextureRecords[index].valid = valid;
  g_todSkyTextureRecords[index].drawnLogged = drawnLogged;
}

void AppendTodSkyTextureRow(
    const char* status,
    IDirect3DBaseTexture9* sourceTexture,
    IDirect3DBaseTexture9* boundTexture,
    const TextureInfo* info,
    unsigned long long hash,
    bool hashValid,
    LONG skyDraws,
    UINT primitiveCount,
    UINT numVertices) {
  const LONG row = InterlockedIncrement(const_cast<LONG*>(&g_todSkyTextureRows));
  char line[1024] = {};
  snprintf(
      line,
      sizeof(line),
      "%ld\t%s\t%p\t%p\t%s0x%016llX\t%u\t%u\t%s\t%u\t0x%08lx\t%u\t%ld\t%u\t%u\r\n",
      row,
      status != nullptr ? status : "unknown",
      sourceTexture,
      boundTexture,
      hashValid ? "" : "invalid:",
      hash,
      info != nullptr ? info->width : 0,
      info != nullptr ? info->height : 0,
      info != nullptr ? FormatName(info->format) : "unknown",
      info != nullptr ? info->levels : 0,
      info != nullptr ? info->usage : 0,
      info != nullptr ? static_cast<unsigned>(info->pool) : 0,
      skyDraws,
      primitiveCount,
      numVertices);

  const char* path =
      g_todSkyTextureLogPath[0] != '\0' ? g_todSkyTextureLogPath : "rtx-remix\\logs\\tod-sky-textures.tsv";
  const bool newFile = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (newFile) {
      const char* header =
          "row\tstatus\tsourceTexture\tboundTexture\trtxTextureHash\twidth\theight\tformat\tlevels\tusage\tpool"
          "\tskyDraw\tprimitiveCount\tnumVertices\r\n";
      WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    }
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

bool TodSkyTexturePendingOrRecorded(IDirect3DBaseTexture9* textureKey) {
  if (textureKey == nullptr || TodSkyTextureRecordExists(textureKey, 0, false)) {
    return true;
  }

  const LONG maxPending = static_cast<LONG>(sizeof(g_todSkyTexturePending) / sizeof(g_todSkyTexturePending[0]));
  const LONG pendingCount = ClampedCount(&g_todSkyTexturePendingCount, maxPending);
  for (LONG i = 0; i < pendingCount; ++i) {
    const IDirect3DBaseTexture9* pendingKey = g_todSkyTexturePending[i].sourceTexture != nullptr
        ? g_todSkyTexturePending[i].sourceTexture
        : g_todSkyTexturePending[i].boundTexture;
    if (pendingKey == textureKey) {
      return true;
    }
  }

  return false;
}

// Whether a marked-draw entry (skyDraws >= 1) for this key is already queued
// this batch, so we only emit one sky_draw_marked per distinct texture.
bool TodSkyDrawPending(IDirect3DBaseTexture9* textureKey) {
  const LONG maxPending = static_cast<LONG>(sizeof(g_todSkyTexturePending) / sizeof(g_todSkyTexturePending[0]));
  const LONG pendingCount = ClampedCount(&g_todSkyTexturePendingCount, maxPending);
  for (LONG i = 0; i < pendingCount; ++i) {
    if (g_todSkyTexturePending[i].skyDraws < 1) {
      continue;
    }
    const IDirect3DBaseTexture9* pendingKey = g_todSkyTexturePending[i].sourceTexture != nullptr
        ? g_todSkyTexturePending[i].sourceTexture
        : g_todSkyTexturePending[i].boundTexture;
    if (pendingKey == textureKey) {
      return true;
    }
  }
  return false;
}

void QueueTodSkyTexture(
    IDirect3DBaseTexture9* sourceTexture,
    IDirect3DBaseTexture9* boundTexture,
    LONG skyDraws,
    UINT primitiveCount,
    UINT numVertices) {
  if (!kEnableTodSkyTextureDiscovery) {
    return;
  }

  IDirect3DBaseTexture9* textureKey = sourceTexture != nullptr ? sourceTexture : boundTexture;
  const bool isDraw = skyDraws >= 1;
  if (!isDraw) {
    // Material bind: dedup against any prior record or pending entry.
    if (TodSkyTexturePendingOrRecorded(textureKey)) {
      return;
    }
  } else {
    // Marked sky draw: allow through even if already bound as a material, so we
    // can record that this texture actually reached a MinZ-marked draw. Only
    // dedup against an already-logged draw or a draw already pending this batch.
    if (TodSkyTextureKeyDrawnLogged(textureKey) || TodSkyDrawPending(textureKey)) {
      return;
    }
  }

  const LONG maxPending = static_cast<LONG>(sizeof(g_todSkyTexturePending) / sizeof(g_todSkyTexturePending[0]));
  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_todSkyTexturePendingCount)) - 1;
  if (index < 0 || index >= maxPending) {
    return;
  }

  if (sourceTexture != nullptr) {
    sourceTexture->AddRef();
  }
  if (boundTexture != nullptr && boundTexture != sourceTexture) {
    boundTexture->AddRef();
  }
  g_todSkyTexturePending[index].sourceTexture = sourceTexture;
  g_todSkyTexturePending[index].boundTexture = boundTexture;
  g_todSkyTexturePending[index].skyDraws = skyDraws;
  g_todSkyTexturePending[index].primitiveCount = primitiveCount;
  g_todSkyTexturePending[index].numVertices = numVertices;
}

void QueueTodSkyTextureForDraw(LONG skyDraws, UINT primitiveCount, UINT numVertices) {
  IDirect3DBaseTexture9* sourceTexture =
      g_currentSourceTexture0 != nullptr ? g_currentSourceTexture0 : g_currentTexture0;
  QueueTodSkyTexture(sourceTexture, g_currentTexture0, skyDraws, primitiveCount, numVertices);
}

void ProcessQueuedTodSkyTextures() {
  if (!kEnableTodSkyTextureDiscovery) {
    return;
  }

  const LONG maxPending = static_cast<LONG>(sizeof(g_todSkyTexturePending) / sizeof(g_todSkyTexturePending[0]));
  LONG pendingCount = InterlockedExchange(const_cast<LONG*>(&g_todSkyTexturePendingCount), 0);
  if (pendingCount > maxPending) {
    pendingCount = maxPending;
  }

  for (LONG i = 0; i < pendingCount; ++i) {
    TodSkyTexturePending pending = g_todSkyTexturePending[i];
    g_todSkyTexturePending[i] = {};

    IDirect3DBaseTexture9* sourceTexture = pending.sourceTexture;
    IDirect3DBaseTexture9* hashTexture = sourceTexture;
    const TextureInfo* info = FindTextureInfo(hashTexture);
    const bool sourceUsable =
        hashTexture != nullptr && info != nullptr && hashTexture->GetType() == D3DRTYPE_TEXTURE;
    bool usedBoundFallback = false;
    if (!sourceUsable && pending.boundTexture != nullptr && pending.boundTexture != sourceTexture) {
      const TextureInfo* boundInfo = FindTextureInfo(pending.boundTexture);
      if (boundInfo != nullptr && pending.boundTexture->GetType() == D3DRTYPE_TEXTURE) {
        hashTexture = pending.boundTexture;
        info = boundInfo;
        usedBoundFallback = true;
      }
    }

    const bool isDraw = pending.skyDraws >= 1;

    if (hashTexture == nullptr || info == nullptr || hashTexture->GetType() != D3DRTYPE_TEXTURE) {
      if (!TodSkyTextureRecordExists(hashTexture, 0, false)) {
        AddTodSkyTextureRecord(hashTexture, 0, false, isDraw);
        AppendTodSkyTextureRow(
            isDraw ? "sky_draw_no_texture_info" : "no_texture_info",
            sourceTexture,
            pending.boundTexture,
            info,
            0,
            false,
            pending.skyDraws,
            pending.primitiveCount,
            pending.numVertices);
      }
      if (sourceTexture != nullptr) {
        sourceTexture->Release();
      }
      if (pending.boundTexture != nullptr && pending.boundTexture != sourceTexture) {
        pending.boundTexture->Release();
      }
      continue;
    }

    unsigned long long rtxHash = 0;
    const bool hashValid = ComputeRtxTextureHash(hashTexture, info, &rtxHash);
    TodSkyTextureRecord* record = FindTodSkyTextureRecord(hashTexture, rtxHash, hashValid);
    if (record == nullptr) {
      // First time we have seen this sky texture at all.
      AddTodSkyTextureRecord(hashTexture, rtxHash, hashValid, isDraw);
      if (hashValid) {
        DumpTodSkyTextureDds(hashTexture, info, rtxHash);
      }
      const char* status = isDraw
          ? "sky_draw_marked"
          : (hashValid ? (usedBoundFallback ? "ok_bound_fallback" : "ok") : "hash_failed");
      AppendTodSkyTextureRow(
          status,
          sourceTexture,
          pending.boundTexture,
          info,
          rtxHash,
          hashValid,
          pending.skyDraws,
          pending.primitiveCount,
          pending.numVertices);
    } else if (isDraw && !record->drawnLogged) {
      // Already seen as a material bind; now confirmed it reaches a MinZ-marked
      // sky draw. Emit a separate row so the marked subset is visible.
      record->drawnLogged = true;
      AppendTodSkyTextureRow(
          "sky_draw_marked",
          sourceTexture,
          pending.boundTexture,
          info,
          rtxHash,
          hashValid,
          pending.skyDraws,
          pending.primitiveCount,
          pending.numVertices);
    }
    if (sourceTexture != nullptr) {
      sourceTexture->Release();
    }
    if (pending.boundTexture != nullptr && pending.boundTexture != sourceTexture) {
      pending.boundTexture->Release();
    }
  }
}

bool TodForcedTextureRecordExists(
    IDirect3DBaseTexture9* texture,
    unsigned long long hash,
    bool valid) {
  const LONG count = ClampedCount(&g_todForcedTextureRecordCount, kMaxTodForcedTextureRecords);
  for (LONG i = 0; i < count; ++i) {
    const TodForcedTextureRecord& record = g_todForcedTextureRecords[i];
    if (record.texture == texture) {
      return true;
    }
    if (valid && record.valid && record.hash == hash) {
      return true;
    }
  }
  return false;
}

void AddTodForcedTextureRecord(
    IDirect3DBaseTexture9* texture,
    unsigned long long hash,
    bool valid) {
  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_todForcedTextureRecordCount)) - 1;
  if (index < 0 || index >= kMaxTodForcedTextureRecords) {
    return;
  }
  g_todForcedTextureRecords[index].texture = texture;
  g_todForcedTextureRecords[index].hash = hash;
  g_todForcedTextureRecords[index].valid = valid;
}

void AppendTodForcedTextureProbeRow(
    const char* status,
    IDirect3DBaseTexture9* sourceTexture,
    IDirect3DBaseTexture9* boundTexture,
    const TextureInfo* info,
    unsigned long long hash,
    bool hashValid,
    LONG probeFrame) {
  const LONG row = InterlockedIncrement(const_cast<LONG*>(&g_todForcedTextureRows));
  char line[1024] = {};
  snprintf(
      line,
      sizeof(line),
      "%ld\t%s\t%p\t%p\t%s0x%016llX\t%u\t%u\t%s\t%u\t0x%08lx\t%u\t%ld\r\n",
      row,
      status != nullptr ? status : "unknown",
      sourceTexture,
      boundTexture,
      hashValid ? "" : "invalid:",
      hash,
      info != nullptr ? info->width : 0,
      info != nullptr ? info->height : 0,
      info != nullptr ? FormatName(info->format) : "unknown",
      info != nullptr ? info->levels : 0,
      info != nullptr ? info->usage : 0,
      info != nullptr ? static_cast<unsigned>(info->pool) : 0,
      probeFrame);

  const char* path = g_todForcedTextureProbeLogPath[0] != '\0'
      ? g_todForcedTextureProbeLogPath
      : "rtx-remix\\logs\\tod-forced-texture-probe.tsv";
  const bool newFile = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (newFile) {
      const char* header =
          "row\tstatus\tsourceTexture\tboundTexture\trtxTextureHash\twidth\theight\tformat\tlevels\tusage\tpool"
          "\tprobeFrame\r\n";
      WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    }
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

bool TodForcedTexturePendingOrRecorded(IDirect3DBaseTexture9* texture) {
  if (texture == nullptr || TodForcedTextureRecordExists(texture, 0, false)) {
    return true;
  }

  const LONG pendingCount = ClampedCount(&g_todForcedTexturePendingCount, kMaxTodForcedTexturePending);
  for (LONG i = 0; i < pendingCount; ++i) {
    if (g_todForcedTexturePending[i].sourceTexture == texture) {
      return true;
    }
  }

  return false;
}

void QueueTodForcedTextureProbeTexture(
    IDirect3DBaseTexture9* sourceTexture,
    IDirect3DBaseTexture9* boundTexture) {
  if (!kEnableTodTextureMapProbe || sourceTexture == nullptr) {
    return;
  }

  const LONG activeFrame =
      InterlockedCompareExchange(const_cast<LONG*>(&g_todTextureMapProbeActive), 0, 0);
  if (activeFrame <= 0 || TodForcedTexturePendingOrRecorded(sourceTexture)) {
    return;
  }

  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_todForcedTexturePendingCount)) - 1;
  if (index < 0 || index >= kMaxTodForcedTexturePending) {
    return;
  }

  sourceTexture->AddRef();
  g_todForcedTexturePending[index].sourceTexture = sourceTexture;
  g_todForcedTexturePending[index].boundTexture = boundTexture;
  g_todForcedTexturePending[index].probeFrame = activeFrame;
}

void ProcessQueuedTodForcedTextureProbeTextures() {
  if (!kEnableTodTextureMapProbe) {
    return;
  }

  LONG pendingCount = InterlockedExchange(const_cast<LONG*>(&g_todForcedTexturePendingCount), 0);
  if (pendingCount > kMaxTodForcedTexturePending) {
    pendingCount = kMaxTodForcedTexturePending;
  }

  for (LONG i = 0; i < pendingCount; ++i) {
    TodForcedTexturePending pending = g_todForcedTexturePending[i];
    g_todForcedTexturePending[i] = {};

    IDirect3DBaseTexture9* sourceTexture = pending.sourceTexture;
    const TextureInfo* info = FindTextureInfo(sourceTexture);
    if (sourceTexture == nullptr || info == nullptr || sourceTexture->GetType() != D3DRTYPE_TEXTURE) {
      if (!TodForcedTextureRecordExists(sourceTexture, 0, false)) {
        AddTodForcedTextureRecord(sourceTexture, 0, false);
        AppendTodForcedTextureProbeRow(
            "no_texture_info",
            sourceTexture,
            pending.boundTexture,
            info,
            0,
            false,
            pending.probeFrame);
      }
      if (sourceTexture != nullptr) {
        sourceTexture->Release();
      }
      continue;
    }

    unsigned long long rtxHash = 0;
    const bool hashValid = ComputeRtxTextureHash(sourceTexture, info, &rtxHash);
    if (!TodForcedTextureRecordExists(sourceTexture, rtxHash, hashValid)) {
      AddTodForcedTextureRecord(sourceTexture, rtxHash, hashValid);
      if (hashValid) {
        DumpTodForcedTextureProbeDds(sourceTexture, info, rtxHash);
      }
      AppendTodForcedTextureProbeRow(
          hashValid ? "ok" : "hash_failed",
          sourceTexture,
          pending.boundTexture,
          info,
          rtxHash,
          hashValid,
          pending.probeFrame);
    }
    sourceTexture->Release();
  }
}

void AppendTodSkyAssetLoadProbeRow(
    const char* status,
    const char* resourcePath,
    void* asset,
    DWORD exceptionCode) {
  const LONG row = InterlockedIncrement(const_cast<LONG*>(&g_todSkyAssetLoadProbeRows));
  char line[1024] = {};
  snprintf(
      line,
      sizeof(line),
      "%ld\t%s\t%p\t0x%08lx\t%s\r\n",
      row,
      status != nullptr ? status : "unknown",
      asset,
      static_cast<unsigned long>(exceptionCode),
      resourcePath != nullptr ? resourcePath : "");

  const char* path = g_todSkyAssetLoadProbeLogPath[0] != '\0'
      ? g_todSkyAssetLoadProbeLogPath
      : "rtx-remix\\logs\\tod-sky-asset-load-probe.tsv";
  const bool newFile = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (newFile) {
      const char* header = "row\tstatus\tasset\texceptionCode\tresourcePath\r\n";
      WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    }
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

void RunTodSkyAssetLoadProbe() {
  if (!kEnableTodSkyAssetLoadProbe) {
    return;
  }

  if (InterlockedExchange(const_cast<LONG*>(&g_todSkyAssetLoadProbeDone), 1) != 0) {
    return;
  }

  const uintptr_t moduleBase = TodModuleBase();
  const uintptr_t loadFunctionAddress = moduleBase + kTodLoadNativeResourceRva;
  const uintptr_t allocatorAddress = moduleBase + kTodTextureAssetAllocatorGlobalRva;
  const uintptr_t loadFlagAddress = moduleBase + kTodTextureAssetLoadFlagGlobalRva;
  if (moduleBase == 0 ||
      !IsCommittedMemoryRange(loadFunctionAddress, 16, false) ||
      !IsCommittedMemoryRange(allocatorAddress, sizeof(int), true) ||
      !IsCommittedMemoryRange(loadFlagAddress, sizeof(BYTE), true)) {
    AppendTodSkyAssetLoadProbeRow("probe_unavailable", "", nullptr, 0);
    return;
  }

  int* textureAllocator = reinterpret_cast<int*>(allocatorAddress);
  BYTE* textureLoadFlag = reinterpret_cast<BYTE*>(loadFlagAddress);
  const int savedAllocator = *textureAllocator;
  const BYTE savedLoadFlag = *textureLoadFlag;
  TodLoadNativeResourceFn loadNativeResource =
      reinterpret_cast<TodLoadNativeResourceFn>(loadFunctionAddress);

  *textureAllocator = kTodTextureAssetAllocatorId;
  *textureLoadFlag = 1;

  const LONG pathCount = static_cast<LONG>(
      sizeof(kTodSkyTextureAssetPaths) / sizeof(kTodSkyTextureAssetPaths[0]));
  for (LONG i = 0; i < pathCount; ++i) {
    const char* assetPath = kTodSkyTextureAssetPaths[i];
    char pathBuffer[260] = {};
    strcpy_s(pathBuffer, assetPath);

    void* asset = nullptr;
    DWORD exceptionCode = 0;
    __try {
      asset = loadNativeResource(pathBuffer);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
      exceptionCode = GetExceptionCode();
    }

    AppendTodSkyAssetLoadProbeRow(
        exceptionCode != 0 ? "exception" : (asset != nullptr ? "loaded" : "null"),
        assetPath,
        asset,
        exceptionCode);
  }

  *textureAllocator = savedAllocator;
  *textureLoadFlag = savedLoadFlag;
}

void RunTodTextureMapProbe() {
  if (!kEnableTodTextureMapProbe) {
    return;
  }

  const LONG currentFrame =
      InterlockedCompareExchange(const_cast<LONG*>(&g_todTextureMapProbeFramesDone), 0, 0);
  if (currentFrame >= kTodTextureMapProbeFrames) {
    return;
  }

  const uintptr_t moduleBase = TodModuleBase();
  const uintptr_t functionAddress = moduleBase + kTodTextureDrawAllTexturesRva;
  if (moduleBase == 0 || !IsCommittedMemoryRange(functionAddress, 16, false)) {
    return;
  }

  const LONG probeFrame = InterlockedIncrement(const_cast<LONG*>(&g_todTextureMapProbeFramesDone));
  if (probeFrame <= 0 || probeFrame > kTodTextureMapProbeFrames) {
    return;
  }

  InterlockedExchange(const_cast<LONG*>(&g_todTextureMapProbeActive), probeFrame);
  reinterpret_cast<TodTextureDrawAllTexturesFn>(functionAddress)();
  InterlockedExchange(const_cast<LONG*>(&g_todTextureMapProbeActive), 0);
}

bool IsRenderTargetTexture(IDirect3DBaseTexture9* texture) {
  const TextureInfo* info = FindTextureInfo(texture);
  return info != nullptr && IsRenderTargetUsage(info->usage);
}

TextureCopyInfo* FindTextureCopyInfo(IDirect3DBaseTexture9* texture) {
  if (texture == nullptr) {
    return nullptr;
  }

  const LONG maxCount = static_cast<LONG>(sizeof(g_textureCopyInfo) / sizeof(g_textureCopyInfo[0]));
  const LONG count = ClampedCount(&g_textureCopyInfoCount, maxCount);
  for (LONG i = 0; i < count; ++i) {
    if (g_textureCopyInfo[i].source == texture) {
      return &g_textureCopyInfo[i];
    }
  }

  return nullptr;
}

TextureCopyInfo* EnsureTextureCopyInfo(IDirect3DBaseTexture9* texture) {
  TextureCopyInfo* existing = FindTextureCopyInfo(texture);
  if (existing != nullptr || texture == nullptr) {
    return existing;
  }

  const LONG maxCount = static_cast<LONG>(sizeof(g_textureCopyInfo) / sizeof(g_textureCopyInfo[0]));
  const LONG count = ClampedCount(&g_textureCopyInfoCount, maxCount);
  if (count >= maxCount) {
    return nullptr;
  }

  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_textureCopyInfoCount)) - 1;
  if (index < 0 || index >= maxCount) {
    return nullptr;
  }

  g_textureCopyInfo[index].source = texture;
  g_textureCopyInfo[index].replacement = nullptr;
  g_textureCopyInfo[index].attempted = false;
  return &g_textureCopyInfo[index];
}

void ResetTextureCopyForTexture(IDirect3DBaseTexture9* texture) {
  TextureCopyInfo* copyInfo = FindTextureCopyInfo(texture);
  if (copyInfo == nullptr) {
    return;
  }

  IDirect3DBaseTexture9* replacement = copyInfo->replacement;
  copyInfo->replacement = nullptr;
  copyInfo->attempted = false;
  if (replacement != nullptr) {
    replacement->Release();
  }
}

void RegisterTexture(
    IDirect3DBaseTexture9* texture,
    IDirect3DSurface9* level0Surface,
    UINT width,
    UINT height,
    UINT levels,
    D3DFORMAT format,
    DWORD usage,
    D3DPOOL pool) {
  if (texture == nullptr) {
    return;
  }

  const LONG maxCount = static_cast<LONG>(sizeof(g_textureInfo) / sizeof(g_textureInfo[0]));
  LONG count = ClampedCount(&g_textureInfoCount, maxCount);
  for (LONG i = 0; i < count; ++i) {
    if (g_textureInfo[i].texture == texture) {
      ResetTextureCopyForTexture(texture);
      g_textureInfo[i].level0Surface = level0Surface;
      g_textureInfo[i].width = width;
      g_textureInfo[i].height = height;
      g_textureInfo[i].levels = levels;
      g_textureInfo[i].format = format;
      g_textureInfo[i].usage = usage;
      g_textureInfo[i].pool = pool;
      return;
    }
  }

  if (count >= maxCount) {
    return;
  }

  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_textureInfoCount)) - 1;
  if (index < 0 || index >= maxCount) {
    return;
  }

  g_textureInfo[index].texture = texture;
  g_textureInfo[index].level0Surface = level0Surface;
  g_textureInfo[index].width = width;
  g_textureInfo[index].height = height;
  g_textureInfo[index].levels = levels;
  g_textureInfo[index].format = format;
  g_textureInfo[index].usage = usage;
  g_textureInfo[index].pool = pool;
}

bool CopyTextureLevel(IDirect3DTexture9* source, IDirect3DTexture9* dest, UINT level) {
  D3DSURFACE_DESC desc = {};
  if (FAILED(source->GetLevelDesc(level, &desc))) {
    return false;
  }

  UINT rowBytes = 0;
  UINT rowCount = 0;
  if (!ComputeTextureCopyLayout(desc.Format, desc.Width, desc.Height, &rowBytes, &rowCount)) {
    return false;
  }

  D3DLOCKED_RECT sourceRect = {};
  if (FAILED(source->LockRect(level, &sourceRect, nullptr, D3DLOCK_READONLY))) {
    return false;
  }

  D3DLOCKED_RECT destRect = {};
  if (FAILED(dest->LockRect(level, &destRect, nullptr, 0))) {
    source->UnlockRect(level);
    return false;
  }

  bool copied = false;
  if (sourceRect.pBits != nullptr && destRect.pBits != nullptr &&
      sourceRect.Pitch > 0 && destRect.Pitch > 0) {
    const UINT bytesPerRow = MinUInt(
        rowBytes,
        static_cast<UINT>(MinInt(sourceRect.Pitch, destRect.Pitch)));
    const BYTE* sourceBytes = static_cast<const BYTE*>(sourceRect.pBits);
    BYTE* destBytes = static_cast<BYTE*>(destRect.pBits);
    for (UINT row = 0; row < rowCount; ++row) {
      memcpy(destBytes + row * destRect.Pitch, sourceBytes + row * sourceRect.Pitch, bytesPerRow);
    }
    copied = true;
  }

  dest->UnlockRect(level);
  source->UnlockRect(level);
  return copied;
}

bool CopyTextureData(IDirect3DTexture9* source, IDirect3DTexture9* dest) {
  if (source == nullptr || dest == nullptr) {
    return false;
  }

  const UINT levels = MinUInt(source->GetLevelCount(), dest->GetLevelCount());
  if (levels == 0) {
    return false;
  }

  for (UINT level = 0; level < levels; ++level) {
    if (!CopyTextureLevel(source, dest, level)) {
      return false;
    }
  }

  return true;
}

IDirect3DBaseTexture9* GetOrCreateDefaultTextureCopy(
    IDirect3DDevice9* device,
    IDirect3DBaseTexture9* texture,
    const TextureInfo* info,
    bool forceManagedA8Copy = false) {
  const bool forcedManagedA8Copy =
      forceManagedA8Copy && IsManagedRegularTexture(info) && info->format == D3DFMT_A8R8G8B8;
  if (!kBindDefaultCopiesForManagedTextures || device == nullptr || texture == nullptr ||
      g_origCreateTexture == nullptr ||
      (!ShouldCreateDefaultTextureCopy(info) && !forcedManagedA8Copy)) {
    return nullptr;
  }

  TextureCopyInfo* copyInfo = EnsureTextureCopyInfo(texture);
  if (copyInfo == nullptr) {
    return nullptr;
  }
  if (copyInfo->attempted) {
    return copyInfo->replacement;
  }
  copyInfo->attempted = true;

  const LONG attempt = InterlockedIncrement(const_cast<LONG*>(&g_textureCopyAttempts));
  IDirect3DTexture9* sourceTexture = reinterpret_cast<IDirect3DTexture9*>(texture);
  D3DSURFACE_DESC desc = {};
  if (FAILED(sourceTexture->GetLevelDesc(0, &desc))) {
    InterlockedIncrement(const_cast<LONG*>(&g_textureCopyFailures));
    return nullptr;
  }

  UINT unusedRowBytes = 0;
  UINT unusedRowCount = 0;
  if (!ComputeTextureCopyLayout(desc.Format, desc.Width, desc.Height, &unusedRowBytes, &unusedRowCount)) {
    if (attempt <= 40 || (attempt % 500) == 0) {
      Log(
          "managed texture copy unsupported #%ld: tex=%p %ux%u fmt=%s(%u)",
          attempt,
          texture,
          desc.Width,
          desc.Height,
          FormatName(desc.Format),
          static_cast<unsigned>(desc.Format));
    }
    InterlockedIncrement(const_cast<LONG*>(&g_textureCopyFailures));
    return nullptr;
  }

  const UINT levels = sourceTexture->GetLevelCount();
  IDirect3DTexture9* systemTexture = nullptr;
  HRESULT result = g_origCreateTexture(
      device,
      desc.Width,
      desc.Height,
      levels,
      0,
      desc.Format,
      D3DPOOL_SYSTEMMEM,
      &systemTexture,
      nullptr);
  if (FAILED(result) || systemTexture == nullptr) {
    if (attempt <= 40 || (attempt % 500) == 0) {
      Log(
          "managed texture copy sysmem create failed #%ld: hr=0x%08lx tex=%p %ux%u fmt=%s(%u) levels=%u",
          attempt,
          static_cast<unsigned long>(result),
          texture,
          desc.Width,
          desc.Height,
          FormatName(desc.Format),
          static_cast<unsigned>(desc.Format),
          levels);
    }
    InterlockedIncrement(const_cast<LONG*>(&g_textureCopyFailures));
    return nullptr;
  }

  IDirect3DTexture9* defaultTexture = nullptr;
  result = g_origCreateTexture(
      device,
      desc.Width,
      desc.Height,
      levels,
      0,
      desc.Format,
      D3DPOOL_DEFAULT,
      &defaultTexture,
      nullptr);
  if (FAILED(result) || defaultTexture == nullptr) {
    if (attempt <= 40 || (attempt % 500) == 0) {
      Log(
          "managed texture copy default create failed #%ld: hr=0x%08lx tex=%p %ux%u fmt=%s(%u) levels=%u",
          attempt,
          static_cast<unsigned long>(result),
          texture,
          desc.Width,
          desc.Height,
          FormatName(desc.Format),
          static_cast<unsigned>(desc.Format),
          levels);
    }
    systemTexture->Release();
    InterlockedIncrement(const_cast<LONG*>(&g_textureCopyFailures));
    return nullptr;
  }

  bool copied = CopyTextureData(sourceTexture, systemTexture);
  if (copied) {
    result = device->UpdateTexture(systemTexture, defaultTexture);
    copied = SUCCEEDED(result);
  }

  systemTexture->Release();
  if (!copied) {
    if (attempt <= 40 || (attempt % 500) == 0) {
      Log(
          "managed texture copy upload failed #%ld: hr=0x%08lx tex=%p copy=%p %ux%u fmt=%s(%u) levels=%u",
          attempt,
          static_cast<unsigned long>(result),
          texture,
          defaultTexture,
          desc.Width,
          desc.Height,
          FormatName(desc.Format),
          static_cast<unsigned>(desc.Format),
          levels);
    }
    defaultTexture->Release();
    InterlockedIncrement(const_cast<LONG*>(&g_textureCopyFailures));
    return nullptr;
  }

  RegisterTexture(defaultTexture, nullptr, desc.Width, desc.Height, levels, desc.Format, 0, D3DPOOL_DEFAULT);
  copyInfo->replacement = defaultTexture;
  const LONG success = InterlockedIncrement(const_cast<LONG*>(&g_textureCopySuccesses));
  if (success <= 80 || (success % 500) == 0) {
    Log(
        "managed texture copy success #%ld: attempt=%ld src=%p dst=%p %ux%u fmt=%s(%u) levels=%u",
        success,
        attempt,
        texture,
        defaultTexture,
        desc.Width,
        desc.Height,
        FormatName(desc.Format),
        static_cast<unsigned>(desc.Format),
        levels);
  }

  return defaultTexture;
}

template <typename Fn>
bool HookVtableSlot(void* object, DWORD index, Fn hook, Fn* original, const char* name) {
  if (object == nullptr || hook == nullptr || original == nullptr) {
    Log("%s hook skipped: invalid input", name);
    return false;
  }

  void*** objectVtable = reinterpret_cast<void***>(object);
  void** vtable = *objectVtable;
  if (vtable == nullptr) {
    Log("%s hook skipped: null vtable", name);
    return false;
  }

  void** slot = &vtable[index];
  const void* hookPtr = reinterpret_cast<const void*>(hook);
  if (*slot == hookPtr) {
    return true;
  }

  DWORD oldProtect = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
    Log("%s hook failed: VirtualProtect error=%lu", name, GetLastError());
    return false;
  }

  if (*original == nullptr) {
    *original = reinterpret_cast<Fn>(*slot);
  }
  void* previous = *slot;
  *slot = const_cast<void*>(hookPtr);

  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

  Log("%s hook installed: slot=%p original=%p hook=%p", name, slot, previous, hookPtr);
  return true;
}

void AppendTodSkyTextureRow(
    const char* status,
    IDirect3DBaseTexture9* sourceTexture,
    IDirect3DBaseTexture9* boundTexture,
    const TextureInfo* info,
    unsigned long long hash,
    bool hashValid,
    LONG skyDraws,
    UINT primitiveCount,
    UINT numVertices);
void QueueTodSkyTexture(
    IDirect3DBaseTexture9* sourceTexture,
    IDirect3DBaseTexture9* boundTexture,
    LONG skyDraws,
    UINT primitiveCount,
    UINT numVertices);

uintptr_t NormalizeTodPointer(void* pointer) {
  return StripTodPointerFlags(reinterpret_cast<uintptr_t>(pointer));
}

bool TodSkyRenderMeshRecorded(void* mesh) {
  const uintptr_t meshAddress = NormalizeTodPointer(mesh);
  if (meshAddress == 0) {
    return false;
  }

  const LONG count = ClampedCount(&g_todSkyMeshRecords, kMaxTodSkyRenderMeshes);
  for (LONG i = 0; i < count; ++i) {
    if (g_todSkyRenderMeshes[i] == meshAddress) {
      return true;
    }
  }

  return false;
}

void RecordTodSkyRenderMesh(void* mesh) {
  const uintptr_t meshAddress = NormalizeTodPointer(mesh);
  if (meshAddress == 0 || TodSkyRenderMeshRecorded(mesh)) {
    return;
  }

  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_todSkyMeshRecords)) - 1;
  if (index < 0 || index >= kMaxTodSkyRenderMeshes) {
    return;
  }

  g_todSkyRenderMeshes[index] = meshAddress;
  if (index < 10) {
    AppendTodSkyTextureRow("sky_mesh_recorded", nullptr, nullptr, nullptr, 0, false, -2, 0, 0);
  }
}

void WriteAbsoluteJump6(BYTE* target, uintptr_t destination, SIZE_T patchBytes) {
  target[0] = 0x68;
  *reinterpret_cast<DWORD*>(target + 1) = static_cast<DWORD>(destination);
  target[5] = 0xC3;
  for (SIZE_T i = 6; i < patchBytes; ++i) {
    target[i] = 0x90;
  }
}

bool InstallInlineJumpHook(
    uintptr_t target,
    void* hook,
    void** original,
    SIZE_T patchBytes,
    const char* name) {
  if (target == 0 || hook == nullptr || original == nullptr || patchBytes < 6) {
    Log("%s inline hook skipped: invalid input", name);
    return false;
  }

  if (!IsCommittedMemoryRange(target, patchBytes, false)) {
    Log("%s inline hook skipped: target unavailable at 0x%p", name, reinterpret_cast<void*>(target));
    return false;
  }

  BYTE* targetBytes = reinterpret_cast<BYTE*>(target);
  const SIZE_T trampolineBytes = patchBytes + 6;
  BYTE* trampoline = reinterpret_cast<BYTE*>(VirtualAlloc(
      nullptr,
      trampolineBytes,
      MEM_COMMIT | MEM_RESERVE,
      PAGE_EXECUTE_READWRITE));
  if (trampoline == nullptr) {
    Log("%s inline hook failed: VirtualAlloc error=%lu", name, GetLastError());
    return false;
  }

  memcpy(trampoline, targetBytes, patchBytes);
  WriteAbsoluteJump6(trampoline + patchBytes, target + patchBytes, 6);

  DWORD oldProtect = 0;
  if (!VirtualProtect(targetBytes, patchBytes, PAGE_EXECUTE_READWRITE, &oldProtect)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    Log("%s inline hook failed: VirtualProtect error=%lu", name, GetLastError());
    return false;
  }

  WriteAbsoluteJump6(targetBytes, reinterpret_cast<uintptr_t>(hook), patchBytes);

  DWORD ignored = 0;
  VirtualProtect(targetBytes, patchBytes, oldProtect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), targetBytes, patchBytes);
  FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineBytes);

  *original = trampoline;
  Log("%s inline hook installed: target=%p trampoline=%p hook=%p", name, targetBytes, trampoline, hook);
  return true;
}

void __fastcall Hook_TodSkyBoxRender(void* self, void* edx) {
  InterlockedIncrement(const_cast<LONG*>(&g_todSkyBoxRenderCalls));
  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyBoxRenderFirstCallLogged), 1, 0) == 0) {
    AppendTodSkyTextureRow("scope_entered", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
  }
  InterlockedIncrement(const_cast<LONG*>(&g_todSkyBoxRenderDepth));
  if (g_origTodSkyBoxRender != nullptr) {
    g_origTodSkyBoxRender(self, edx);
  }
  InterlockedDecrement(const_cast<LONG*>(&g_todSkyBoxRenderDepth));
}

bool TryGetTodMaterialD3DTexture(void* material, IDirect3DBaseTexture9** texture) {
  if (texture == nullptr) {
    return false;
  }
  *texture = nullptr;

  const uintptr_t materialAddress = reinterpret_cast<uintptr_t>(material);
  if (materialAddress == 0 ||
      !IsCommittedMemoryRange(materialAddress + sizeof(uintptr_t), sizeof(uintptr_t), false)) {
    return false;
  }

  uintptr_t textureAddress = 0;
  if (!TryReadPointer(materialAddress + sizeof(uintptr_t), &textureAddress)) {
    return false;
  }

  textureAddress = StripTodPointerFlags(textureAddress);
  if (textureAddress == 0) {
    return false;
  }

  uintptr_t vtable = 0;
  if (!TryReadPointer(textureAddress, &vtable)) {
    return false;
  }

  *texture = reinterpret_cast<IDirect3DBaseTexture9*>(textureAddress);
  return true;
}

void __fastcall Hook_TodRenderListSetMaterial(
    void* self,
    void* edx,
    void* material,
    DWORD stage) {
  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyBoxRenderDepth), 0, 0) > 0) {
    InterlockedIncrement(const_cast<LONG*>(&g_todSkyMaterialCommands));
    IDirect3DBaseTexture9* texture = nullptr;
    if (stage == 0 && TryGetTodMaterialD3DTexture(material, &texture)) {
      QueueTodSkyTexture(texture, texture, 0, 0, 0);
      InterlockedIncrement(const_cast<LONG*>(&g_todSkyScopedTextureQueues));
    } else {
      AppendTodSkyTextureRow("scope_material_no_texture", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    }
  }

  if (g_origTodRenderListSetMaterial != nullptr) {
    g_origTodRenderListSetMaterial(self, edx, material, stage);
  }
}

void __fastcall Hook_TodRenderListAddMesh(void* self, void* edx, void* mesh) {
  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyBoxRenderDepth), 0, 0) > 0) {
    InterlockedIncrement(const_cast<LONG*>(&g_todSkyMeshCommands));
    RecordTodSkyRenderMesh(mesh);
  }

  if (g_origTodRenderListAddMesh != nullptr) {
    g_origTodRenderListAddMesh(self, edx, mesh);
  }
}

void __fastcall Hook_TodRenderMeshDraw(void* self, void* edx, void* mesh) {
  const bool skyMesh = TodSkyRenderMeshRecorded(mesh);
  if (skyMesh) {
    const LONG executions = InterlockedIncrement(const_cast<LONG*>(&g_todSkyMeshDrawExecutions));
    if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyMeshDrawFirstLogged), 1, 0) == 0) {
      AppendTodSkyTextureRow(
          "sky_mesh_draw_entered", nullptr, nullptr, nullptr, 0, false, executions, 0, 0);
    }
    InterlockedIncrement(const_cast<LONG*>(&g_todSkyMeshDrawDepth));
  }

  if (g_origTodRenderMeshDraw != nullptr) {
    g_origTodRenderMeshDraw(self, edx, mesh);
  }

  if (skyMesh) {
    InterlockedDecrement(const_cast<LONG*>(&g_todSkyMeshDrawDepth));
  }
}

void InstallTodRenderListSetMaterialHook() {
  if (!kHookTodSkyBoxRenderTextureScope) {
    return;
  }

  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todRenderListSetMaterialHooked), 1, 0) != 0) {
    return;
  }

  const uintptr_t moduleBase = TodModuleBase();
  const uintptr_t target = moduleBase != 0 ? moduleBase + kTodRenderListSetMaterialRva : 0;
  if (target == 0 || !IsCommittedMemoryRange(target, kTodRenderListSetMaterialPatchBytes, false)) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderListSetMaterialHooked), 0);
    AppendTodSkyTextureRow("material_hook_unavailable", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  BYTE* targetBytes = reinterpret_cast<BYTE*>(target);
  memcpy(g_todRenderListSetMaterialOriginalBytes, targetBytes, kTodRenderListSetMaterialPatchBytes);
  if (targetBytes[0] != 0x56 || targetBytes[1] != 0x57 ||
      targetBytes[2] != 0x8B || targetBytes[3] != 0xF9) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderListSetMaterialHooked), 0);
    AppendTodSkyTextureRow("material_hook_unexpected_prologue", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  void* original = nullptr;
  if (!InstallInlineJumpHook(
          target,
          reinterpret_cast<void*>(Hook_TodRenderListSetMaterial),
          &original,
          kTodRenderListSetMaterialPatchBytes,
          "TOD RenderList::SetMaterial")) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderListSetMaterialHooked), 0);
    AppendTodSkyTextureRow("material_hook_install_failed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  g_origTodRenderListSetMaterial =
      reinterpret_cast<TodRenderListSetMaterialFn>(original);
  AppendTodSkyTextureRow("material_hook_installed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
}

void InstallTodRenderListAddMeshHook() {
  if (!kHookTodSkyBoxRenderTextureScope) {
    return;
  }

  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todRenderListAddMeshHooked), 1, 0) != 0) {
    return;
  }

  const uintptr_t moduleBase = TodModuleBase();
  const uintptr_t target = moduleBase != 0 ? moduleBase + kTodRenderListAddMeshRva : 0;
  if (target == 0 || !IsCommittedMemoryRange(target, kTodRenderListAddMeshPatchBytes, false)) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderListAddMeshHooked), 0);
    AppendTodSkyTextureRow("mesh_add_hook_unavailable", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  BYTE* targetBytes = reinterpret_cast<BYTE*>(target);
  memcpy(g_todRenderListAddMeshOriginalBytes, targetBytes, kTodRenderListAddMeshPatchBytes);
  if (targetBytes[0] != 0x8B || targetBytes[1] != 0x41 || targetBytes[2] != 0x20 ||
      targetBytes[3] != 0x56 || targetBytes[4] != 0x8D) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderListAddMeshHooked), 0);
    AppendTodSkyTextureRow("mesh_add_hook_unexpected_prologue", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  void* original = nullptr;
  if (!InstallInlineJumpHook(
          target,
          reinterpret_cast<void*>(Hook_TodRenderListAddMesh),
          &original,
          kTodRenderListAddMeshPatchBytes,
          "TOD RenderList::AddMesh")) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderListAddMeshHooked), 0);
    AppendTodSkyTextureRow("mesh_add_hook_install_failed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  g_origTodRenderListAddMesh = reinterpret_cast<TodRenderListAddMeshFn>(original);
  AppendTodSkyTextureRow("mesh_add_hook_installed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
}

void InstallTodRenderMeshDrawHook() {
  if (!kHookTodSkyBoxRenderTextureScope) {
    return;
  }

  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todRenderMeshDrawHooked), 1, 0) != 0) {
    return;
  }

  const uintptr_t moduleBase = TodModuleBase();
  const uintptr_t target = moduleBase != 0 ? moduleBase + kTodRenderMeshDrawRva : 0;
  if (target == 0 || !IsCommittedMemoryRange(target, kTodRenderMeshDrawPatchBytes, false)) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderMeshDrawHooked), 0);
    AppendTodSkyTextureRow("mesh_draw_hook_unavailable", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  BYTE* targetBytes = reinterpret_cast<BYTE*>(target);
  memcpy(g_todRenderMeshDrawOriginalBytes, targetBytes, kTodRenderMeshDrawPatchBytes);
  if (targetBytes[0] != 0x83 || targetBytes[1] != 0xEC || targetBytes[2] != 0x34 ||
      targetBytes[3] != 0x56 || targetBytes[4] != 0x57 ||
      targetBytes[5] != 0x8B || targetBytes[6] != 0x7C) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderMeshDrawHooked), 0);
    AppendTodSkyTextureRow("mesh_draw_hook_unexpected_prologue", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  void* original = nullptr;
  if (!InstallInlineJumpHook(
          target,
          reinterpret_cast<void*>(Hook_TodRenderMeshDraw),
          &original,
          kTodRenderMeshDrawPatchBytes,
          "TOD RenderMesh::Draw")) {
    InterlockedExchange(const_cast<LONG*>(&g_todRenderMeshDrawHooked), 0);
    AppendTodSkyTextureRow("mesh_draw_hook_install_failed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  g_origTodRenderMeshDraw = reinterpret_cast<TodRenderMeshDrawFn>(original);
  AppendTodSkyTextureRow("mesh_draw_hook_installed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
}

void InstallTodSkyBoxRenderHook() {
  if (!kHookTodSkyBoxRenderTextureScope) {
    return;
  }

  if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyBoxRenderHooked), 1, 0) != 0) {
    return;
  }

  const uintptr_t moduleBase = TodModuleBase();
  const uintptr_t target = moduleBase != 0 ? moduleBase + kTodSkyBoxRenderRva : 0;
  if (target == 0 || !IsCommittedMemoryRange(target, kInlineHookPatchBytes, false)) {
    InterlockedExchange(const_cast<LONG*>(&g_todSkyBoxRenderHooked), 0);
    AppendTodSkyTextureRow("scope_hook_unavailable", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    Log("SkyBox::Render hook skipped: target unavailable");
    return;
  }

  BYTE* targetBytes = reinterpret_cast<BYTE*>(target);
  memcpy(g_todSkyBoxRenderOriginalBytes, targetBytes, kInlineHookPatchBytes);
  if (targetBytes[0] != 0x81 || targetBytes[1] != 0xEC) {
    InterlockedExchange(const_cast<LONG*>(&g_todSkyBoxRenderHooked), 0);
    AppendTodSkyTextureRow("scope_hook_unexpected_prologue", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    Log(
        "SkyBox::Render hook skipped: unexpected prologue %02X %02X",
        targetBytes[0],
        targetBytes[1]);
    return;
  }

  void* original = nullptr;
  if (!InstallInlineJumpHook(
          target,
          reinterpret_cast<void*>(Hook_TodSkyBoxRender),
          &original,
          kInlineHookPatchBytes,
          "TOD SkyBox::Render")) {
    InterlockedExchange(const_cast<LONG*>(&g_todSkyBoxRenderHooked), 0);
    AppendTodSkyTextureRow("scope_hook_install_failed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
    return;
  }

  g_origTodSkyBoxRender = reinterpret_cast<TodSkyBoxRenderFn>(original);
  AppendTodSkyTextureRow("scope_hook_installed", nullptr, nullptr, nullptr, 0, false, -1, 0, 0);
}

void LogMatrixSample(const char* label, LONG count, const D3DMATRIX& matrix) {
  if (count <= 5 || (count % 1000) == 0) {
    Log(
        "%s #%ld: "
        "[%.4f %.4f %.4f %.4f] "
        "[%.4f %.4f %.4f %.4f] "
        "[%.4f %.4f %.4f %.4f] "
        "[%.4f %.4f %.4f %.4f]",
        label,
        count,
        matrix._11,
        matrix._12,
        matrix._13,
        matrix._14,
        matrix._21,
        matrix._22,
        matrix._23,
        matrix._24,
        matrix._31,
        matrix._32,
        matrix._33,
        matrix._34,
        matrix._41,
        matrix._42,
        matrix._43,
        matrix._44);
  }
}

bool VertexElementsHaveUsage(
    const D3DVERTEXELEMENT9* elements,
    BYTE usage,
    const char* label,
    bool* anyPosition) {
  if (anyPosition != nullptr) {
    *anyPosition = false;
  }

  if (elements == nullptr) {
    return false;
  }

  bool found = false;
  for (int i = 0; i < 128; ++i) {
    const D3DVERTEXELEMENT9& elem = elements[i];
    if (elem.Stream == 0xFF && elem.Type == D3DDECLTYPE_UNUSED) {
      break;
    }

    if (elem.Usage == D3DDECLUSAGE_POSITION && anyPosition != nullptr) {
      *anyPosition = true;
    }
    if (elem.Usage == usage) {
      found = true;
    }

    if (label != nullptr && i < 8) {
      Log(
          "%s elem[%d]: stream=%u offset=%u type=%u method=%u usage=%u usageIndex=%u",
          label,
          i,
          elem.Stream,
          elem.Offset,
          elem.Type,
          elem.Method,
          elem.Usage,
          elem.UsageIndex);
    }
  }

  return found;
}

void RegisterDeclaration(
    IDirect3DVertexDeclaration9* declaration,
    bool hasPosition,
    bool hasPositionT) {
  if (declaration == nullptr) {
    return;
  }

  LONG existingCount = InterlockedCompareExchange(
      const_cast<LONG*>(&g_declInfoCount),
      0,
      0);
  const LONG maxCount = static_cast<LONG>(sizeof(g_declInfo) / sizeof(g_declInfo[0]));
  if (existingCount > maxCount) {
    existingCount = maxCount;
  }
  for (LONG i = 0; i < existingCount; ++i) {
    if (g_declInfo[i].declaration == declaration) {
      g_declInfo[i].hasPosition = hasPosition;
      g_declInfo[i].hasPositionT = hasPositionT;
      return;
    }
  }

  if (existingCount >= maxCount) {
    return;
  }

  const LONG index = InterlockedIncrement(const_cast<LONG*>(&g_declInfoCount)) - 1;
  if (index >= 0 && index < maxCount) {
    g_declInfo[index].declaration = declaration;
    g_declInfo[index].hasPosition = hasPosition;
    g_declInfo[index].hasPositionT = hasPositionT;
  }
}

bool DeclarationHasPositionT(IDirect3DVertexDeclaration9* declaration) {
  if (declaration == nullptr) {
    return false;
  }

  LONG count = InterlockedCompareExchange(const_cast<LONG*>(&g_declInfoCount), 0, 0);
  const LONG maxCount = static_cast<LONG>(sizeof(g_declInfo) / sizeof(g_declInfo[0]));
  if (count > maxCount) {
    count = maxCount;
  }
  for (LONG i = 0; i < count; ++i) {
    if (g_declInfo[i].declaration == declaration) {
      return g_declInfo[i].hasPositionT;
    }
  }

  return false;
}

LayoutState GetLayoutState() {
  LayoutState state = {};
  state.fvf = g_currentFvf;
  state.declaration = g_currentDeclaration;
  state.vertexShader = g_currentVertexShader;
  state.fvfRhw = IsPreTransformedFvf(g_currentFvf);
  state.declarationPositionT = DeclarationHasPositionT(g_currentDeclaration);
  state.shaderActive = g_currentVertexShader != nullptr;
  state.unknown = g_currentFvf == 0 && g_currentDeclaration == nullptr;
  return state;
}

bool ResendCameraForDraw(IDirect3DDevice9* device, bool skipForLayout) {
  if (!kEnableCameraResend || device == nullptr || g_origSetTransform == nullptr || skipForLayout) {
    return false;
  }

  if (InterlockedCompareExchange(const_cast<LONG*>(&g_currentViewIsIdentity), 0, 0) != 0) {
    InterlockedIncrement(const_cast<LONG*>(&g_identityViewResendSkips));
    return false;
  }

  const LONG depth = InterlockedIncrement(const_cast<LONG*>(&g_resendDepth));
  if (depth != 1) {
    InterlockedDecrement(const_cast<LONG*>(&g_resendDepth));
    return false;
  }

  const bool haveView = InterlockedCompareExchange(const_cast<LONG*>(&g_haveView), 0, 0) != 0;
  const bool haveProjection =
      InterlockedCompareExchange(const_cast<LONG*>(&g_haveProjection), 0, 0) != 0;

  if (haveView) {
    g_origSetTransform(device, D3DTS_VIEW, &g_lastView);
  }
  if (haveProjection) {
    g_origSetTransform(device, D3DTS_PROJECTION, &g_lastProjection);
  }
  if (haveView || haveProjection) {
    InterlockedIncrement(const_cast<LONG*>(&g_resendCalls));
  }

  InterlockedDecrement(const_cast<LONG*>(&g_resendDepth));
  return haveView || haveProjection;
}

bool IsPreTransformedAfterWorldA8Draw(const LayoutState& layout) {
  if (!kBindPreTransformedA8CopiesAfterWorld) {
    return false;
  }

  if (!(layout.fvfRhw || layout.declarationPositionT)) {
    return false;
  }

  if (InterlockedCompareExchange(const_cast<LONG*>(&g_worldDrawnThisFrame), 0, 0) == 0) {
    return false;
  }

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  if (!IsPrimarySurfaceInfo(rtInfo)) {
    return false;
  }

  const TextureInfo* texInfo = FindTextureInfo(g_currentTexture0);
  return IsManagedRegularTexture(texInfo) && texInfo->format == D3DFMT_A8R8G8B8;
}

bool IsManagedA8DrawCopyCandidate(const LayoutState& layout) {
  if (!kBindManagedA8DrawCopies) {
    return false;
  }

  if (layout.fvfRhw || layout.declarationPositionT || layout.shaderActive) {
    return false;
  }

  const bool alphaBlendEnabled =
      InterlockedCompareExchange(const_cast<LONG*>(&g_alphaBlendEnable), 0, 0) != 0;
  const bool alphaTestEnabled =
      InterlockedCompareExchange(const_cast<LONG*>(&g_alphaTestEnable), 0, 0) != 0;

  const bool depthWriteEnabled =
      InterlockedCompareExchange(const_cast<LONG*>(&g_zWriteEnable), 0, 0) != 0;
  const bool depthEnabled =
      InterlockedCompareExchange(const_cast<LONG*>(&g_zEnable), 0, 0) != 0;

  const bool depthTestedParticle = depthEnabled && !depthWriteEnabled;
  const bool alphaTestedCutout =
      depthEnabled && depthWriteEnabled && alphaTestEnabled && !alphaBlendEnabled;
  const bool alphaBlendedWorldCutout =
      depthEnabled && depthWriteEnabled && alphaBlendEnabled;
  if (!depthTestedParticle && !alphaTestedCutout && !alphaBlendedWorldCutout) {
    return false;
  }

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  if (!IsPrimarySurfaceInfo(rtInfo)) {
    return false;
  }

  const TextureInfo* texInfo = FindTextureInfo(g_currentTexture0);
  return IsManagedRegularTexture(texInfo) && texInfo->format == D3DFMT_A8R8G8B8;
}

IDirect3DBaseTexture9* BindPreTransformedA8CopyForDraw(
    IDirect3DDevice9* self,
    const LayoutState& layout) {
  const bool preTransformedUi = IsPreTransformedAfterWorldA8Draw(layout);
  const bool managedA8Draw = IsManagedA8DrawCopyCandidate(layout);
  if ((!preTransformedUi && !managedA8Draw) || g_origSetTexture == nullptr) {
    return nullptr;
  }

  IDirect3DBaseTexture9* original = g_currentTexture0;
  const TextureInfo* texInfo = FindTextureInfo(original);
  IDirect3DBaseTexture9* replacement = GetOrCreateDefaultTextureCopy(self, original, texInfo, true);
  if (replacement == nullptr || replacement == original) {
    return nullptr;
  }

  original->AddRef();
  const HRESULT result = g_origSetTexture(self, 0, replacement);
  if (SUCCEEDED(result)) {
    if (managedA8Draw) {
      InterlockedIncrement(const_cast<LONG*>(&g_managedA8DrawCopyBinds));
    } else {
      InterlockedIncrement(const_cast<LONG*>(&g_preTransformedA8CopyBinds));
    }
    return original;
  }

  original->Release();
  return nullptr;
}

void RestoreTexture0AfterDraw(IDirect3DDevice9* self, IDirect3DBaseTexture9* original) {
  if (original == nullptr) {
    return;
  }

  if (g_origSetTexture != nullptr) {
    g_origSetTexture(self, 0, original);
  }
  original->Release();
}

void MarkWorldDrawIfPrimary(const LayoutState& layout) {
  if (layout.fvfRhw || layout.declarationPositionT) {
    return;
  }

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  if (IsPrimarySurfaceInfo(rtInfo)) {
    InterlockedExchange(const_cast<LONG*>(&g_worldDrawnThisFrame), 1);
  }
}

void LogDrawSample(
    const char* drawName,
    LONG count,
    D3DPRIMITIVETYPE primitiveType,
    UINT primitiveCount,
    UINT vertexCount,
    const LayoutState& layout,
    bool resent) {
  const bool preTransformed = layout.fvfRhw || layout.declarationPositionT;
  const bool interesting =
      count <= 20 || (count % 5000) == 0 || (preTransformed && count <= 1000);
  if (!interesting) {
    return;
  }

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  const TextureInfo* texInfo = FindTextureInfo(g_currentTexture0);

  Log(
      "%s #%ld: primType=%u primCount=%u vertices=%u fvf=0x%08lx fvfRHW=%d decl=%p "
      "declPOSITIONT=%d vs=%p shader=%d unknown=%d resent=%d rt0=%p "
      "rt0Info=%ux%u/%s rtFromTex=%d tex0=%p tex0RT=%d tex0Info=%ux%u/%s "
      "tex0Usage=0x%08lx tex0Pool=%u tex0Levels=%u",
      drawName,
      count,
      static_cast<unsigned>(primitiveType),
      primitiveCount,
      vertexCount,
      layout.fvf,
      layout.fvfRhw ? 1 : 0,
      layout.declaration,
      layout.declarationPositionT ? 1 : 0,
      layout.vertexShader,
      layout.shaderActive ? 1 : 0,
      layout.unknown ? 1 : 0,
      resent ? 1 : 0,
      g_currentRenderTarget0,
      rtInfo != nullptr ? rtInfo->width : 0,
      rtInfo != nullptr ? rtInfo->height : 0,
      rtInfo != nullptr ? FormatName(rtInfo->format) : "unknown",
      rtInfo != nullptr && rtInfo->fromTexture ? 1 : 0,
      g_currentTexture0,
      texInfo != nullptr && IsRenderTargetUsage(texInfo->usage) ? 1 : 0,
      texInfo != nullptr ? texInfo->width : 0,
      texInfo != nullptr ? texInfo->height : 0,
      texInfo != nullptr ? FormatName(texInfo->format) : "unknown",
      texInfo != nullptr ? texInfo->usage : 0,
      texInfo != nullptr ? static_cast<unsigned>(texInfo->pool) : 0,
      texInfo != nullptr ? texInfo->levels : 0);
}

void LogSummary(const char* reason) {
  Log(
      "summary[%s]: presents=%ld indexed=%ld fixedIndexed=%ld preTIndexed=%ld "
      "shaderIndexed=%ld unknownIndexed=%ld primitive=%ld preTPrimitive=%ld "
      "indexedUP=%ld primitiveUP=%ld preTUP=%ld resends=%ld viewSets=%ld "
      "projSets=%ld ignoredIdentityViews=%ld fvfSets=%ld declCreates=%ld "
      "declSets=%ld vsSets=%ld",
      reason,
      InterlockedCompareExchange(const_cast<LONG*>(&g_presentCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_indexedDrawCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_fixedFunctionIndexedDraws), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_preTransformedIndexedDraws), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_shaderIndexedDraws), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_unknownIndexedDraws), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_primitiveDrawCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_preTransformedPrimitiveDraws), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_indexedUpDrawCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_primitiveUpDrawCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_preTransformedUpDraws), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_resendCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setViewCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setProjectionCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_ignoredIdentityViews), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setFvfCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_createDeclarationCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setDeclarationCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setVertexShaderCalls), 0, 0));

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  const TextureInfo* texInfo = FindTextureInfo(g_currentTexture0);
  Log(
      "summary[%s] targets: rt0=%p rt0Info=%ux%u/%s fromTex=%d depth=%p tex0=%p "
      "tex0RT=%d tex0Info=%ux%u/%s surfaces=%ld textures=%ld createTex=%ld "
      "createRT=%ld setRT=%ld redirectedRT=%ld setDS=%ld setTex=%ld stretch=%ld "
      "preloadTex=%ld copyAttempts=%ld copySuccess=%ld copyFailures=%ld copyBinds=%ld "
      "skippedRTComposite=%ld",
      reason,
      g_currentRenderTarget0,
      rtInfo != nullptr ? rtInfo->width : 0,
      rtInfo != nullptr ? rtInfo->height : 0,
      rtInfo != nullptr ? FormatName(rtInfo->format) : "unknown",
      rtInfo != nullptr && rtInfo->fromTexture ? 1 : 0,
      g_currentDepthStencil,
      g_currentTexture0,
      texInfo != nullptr && IsRenderTargetUsage(texInfo->usage) ? 1 : 0,
      texInfo != nullptr ? texInfo->width : 0,
      texInfo != nullptr ? texInfo->height : 0,
      texInfo != nullptr ? FormatName(texInfo->format) : "unknown",
      InterlockedCompareExchange(const_cast<LONG*>(&g_surfaceInfoCount), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_textureInfoCount), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_createTextureCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_createRenderTargetCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setRenderTargetCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_redirectedRenderTargetCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setDepthStencilCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_setTextureCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_stretchRectCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_preloadTextureCalls), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_textureCopyAttempts), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_textureCopySuccesses), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_textureCopyFailures), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_textureCopyBinds), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_skippedRtCompositeDraws), 0, 0));
}

HRESULT APIENTRY Hook_CreateTexture(
    IDirect3DDevice9* self,
    UINT width,
    UINT height,
    UINT levels,
    DWORD usage,
    D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DTexture9** texture,
    HANDLE* sharedHandle) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_createTextureCalls));
  const HRESULT result = g_origCreateTexture(
      self,
      width,
      height,
      levels,
      usage,
      format,
      pool,
      texture,
      sharedHandle);

  IDirect3DSurface9* level0Surface = nullptr;
  if (SUCCEEDED(result) && texture != nullptr && *texture != nullptr) {
    RegisterTexture(*texture, nullptr, width, height, levels, format, usage, pool);

    if (IsRenderTargetUsage(usage) && SUCCEEDED((*texture)->GetSurfaceLevel(0, &level0Surface))) {
      RegisterSurface(level0Surface, *texture, width, height, format, usage, pool, true);
      RegisterTexture(*texture, level0Surface, width, height, levels, format, usage, pool);
      level0Surface->Release();
    }
  }

  if (count <= 50 || IsRenderTargetUsage(usage)) {
    Log(
        "CreateTexture #%ld: hr=0x%08lx tex=%p level0=%p %ux%u levels=%u "
        "fmt=%s(%u) usage=0x%08lx rt=%d pool=%u",
        count,
        static_cast<unsigned long>(result),
        texture != nullptr ? *texture : nullptr,
        level0Surface,
        width,
        height,
        levels,
        FormatName(format),
        static_cast<unsigned>(format),
        usage,
        IsRenderTargetUsage(usage) ? 1 : 0,
        static_cast<unsigned>(pool));
  }

  return result;
}

HRESULT APIENTRY Hook_CreateRenderTarget(
    IDirect3DDevice9* self,
    UINT width,
    UINT height,
    D3DFORMAT format,
    D3DMULTISAMPLE_TYPE multiSample,
    DWORD multisampleQuality,
    BOOL lockable,
    IDirect3DSurface9** surface,
    HANDLE* sharedHandle) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_createRenderTargetCalls));
  const HRESULT result = g_origCreateRenderTarget(
      self,
      width,
      height,
      format,
      multiSample,
      multisampleQuality,
      lockable,
      surface,
      sharedHandle);

  if (SUCCEEDED(result) && surface != nullptr && *surface != nullptr) {
    RegisterSurface(
        *surface,
        nullptr,
        width,
        height,
        format,
        D3DUSAGE_RENDERTARGET,
        D3DPOOL_DEFAULT,
        false);
  }

  Log(
      "CreateRenderTarget #%ld: hr=0x%08lx surface=%p %ux%u fmt=%s(%u) "
      "ms=%u msq=%lu lockable=%d",
      count,
      static_cast<unsigned long>(result),
      surface != nullptr ? *surface : nullptr,
      width,
      height,
      FormatName(format),
      static_cast<unsigned>(format),
      static_cast<unsigned>(multiSample),
      multisampleQuality,
      lockable ? 1 : 0);

  return result;
}

HRESULT APIENTRY Hook_SetRenderTarget(
    IDirect3DDevice9* self,
    DWORD renderTargetIndex,
    IDirect3DSurface9* surface) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setRenderTargetCalls));
  SurfaceInfo* requestedInfo = EnsureSurfaceInfo(surface, false);
  IDirect3DSurface9* actualSurface = surface;
  bool redirected = false;

  const bool shouldRedirect =
      kRedirectAllColorRtTexturesToPrimary
          ? IsColorRenderTargetTextureSurface(requestedInfo)
          : IsFullSizeSceneRenderTarget(requestedInfo);
  if (renderTargetIndex == 0 && kRedirectFullSizeSceneRtToPrimary && shouldRedirect &&
      g_primaryRenderTarget != nullptr) {
    actualSurface = g_primaryRenderTarget;
    redirected = true;
  }

  HRESULT result = g_origSetRenderTarget(self, renderTargetIndex, actualSurface);
  if (FAILED(result) && redirected) {
    actualSurface = surface;
    redirected = false;
    result = g_origSetRenderTarget(self, renderTargetIndex, actualSurface);
  }

  SurfaceInfo* actualInfo = nullptr;
  if (SUCCEEDED(result)) {
    actualInfo = EnsureSurfaceInfo(actualSurface, false);
    if (renderTargetIndex == 0) {
      g_currentRenderTarget0 = actualSurface;
      if (IsPrimarySurfaceInfo(actualInfo)) {
        g_primaryRenderTarget = actualSurface;
      }
      if (redirected) {
        InterlockedIncrement(const_cast<LONG*>(&g_redirectedRenderTargetCalls));
      }
    }
  }

  if (count <= 80 || redirected || (renderTargetIndex == 0 && (count % 500) == 0)) {
    Log(
        "SetRenderTarget #%ld: hr=0x%08lx index=%lu requested=%p reqInfo=%ux%u/%s "
        "reqUsage=0x%08lx reqFromTex=%d actual=%p actualInfo=%ux%u/%s "
        "actualUsage=0x%08lx actualFromTex=%d redirected=%d",
        count,
        static_cast<unsigned long>(result),
        renderTargetIndex,
        surface,
        requestedInfo != nullptr ? requestedInfo->width : 0,
        requestedInfo != nullptr ? requestedInfo->height : 0,
        requestedInfo != nullptr ? FormatName(requestedInfo->format) : "unknown",
        requestedInfo != nullptr ? requestedInfo->usage : 0,
        requestedInfo != nullptr && requestedInfo->fromTexture ? 1 : 0,
        actualSurface,
        actualInfo != nullptr ? actualInfo->width : 0,
        actualInfo != nullptr ? actualInfo->height : 0,
        actualInfo != nullptr ? FormatName(actualInfo->format) : "unknown",
        actualInfo != nullptr ? actualInfo->usage : 0,
        actualInfo != nullptr && actualInfo->fromTexture ? 1 : 0,
        redirected ? 1 : 0);
  }

  return result;
}

HRESULT APIENTRY Hook_SetDepthStencilSurface(IDirect3DDevice9* self, IDirect3DSurface9* surface) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setDepthStencilCalls));
  const HRESULT result = g_origSetDepthStencilSurface(self, surface);

  SurfaceInfo* info = nullptr;
  if (SUCCEEDED(result)) {
    g_currentDepthStencil = surface;
    info = EnsureSurfaceInfo(surface, false);
  }

  if (count <= 40 || (count % 500) == 0) {
    Log(
        "SetDepthStencilSurface #%ld: hr=0x%08lx surface=%p info=%ux%u/%s usage=0x%08lx",
        count,
        static_cast<unsigned long>(result),
        surface,
        info != nullptr ? info->width : 0,
        info != nullptr ? info->height : 0,
        info != nullptr ? FormatName(info->format) : "unknown",
        info != nullptr ? info->usage : 0);
  }

  return result;
}

HRESULT APIENTRY Hook_SetTexture(
    IDirect3DDevice9* self,
    DWORD stage,
    IDirect3DBaseTexture9* texture) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setTextureCalls));
  const TextureInfo* info = FindTextureInfo(texture);
  const bool isManagedRegularTexture = IsManagedRegularTexture(info);
  const LayoutState layout = GetLayoutState();
  const bool preTransformedLayout = layout.fvfRhw || layout.declarationPositionT;
  IDirect3DBaseTexture9* actualTexture = texture;
  IDirect3DBaseTexture9* defaultCopy = GetOrCreateDefaultTextureCopy(self, texture, info);
  bool copiedTextureBound = false;
  if (defaultCopy != nullptr) {
    actualTexture = defaultCopy;
    copiedTextureBound = true;
    InterlockedIncrement(const_cast<LONG*>(&g_textureCopyBinds));
  }

  LONG preloadCount = 0;

  if (kPreloadManagedTexturesBeforeBind && actualTexture != nullptr && isManagedRegularTexture) {
    actualTexture->PreLoad();
    preloadCount = InterlockedIncrement(const_cast<LONG*>(&g_preloadTextureCalls));
    if (preloadCount <= 80 || (preloadCount % 1000) == 0) {
      Log(
          "PreLoad texture #%ld: setTex=%ld stage=%lu tex=%p bound=%p info=%ux%u/%s "
          "usage=0x%08lx pool=%u levels=%u level0=%p",
          preloadCount,
          count,
          stage,
          texture,
          actualTexture,
          info != nullptr ? info->width : 0,
          info != nullptr ? info->height : 0,
          info != nullptr ? FormatName(info->format) : "unknown",
          info != nullptr ? info->usage : 0,
          info != nullptr ? static_cast<unsigned>(info->pool) : 0,
          info != nullptr ? info->levels : 0,
          info != nullptr ? info->level0Surface : nullptr);
    }
  }

  const HRESULT result = g_origSetTexture(self, stage, actualTexture);

  if (SUCCEEDED(result) && stage == 0) {
    g_currentSourceTexture0 = texture;
    g_currentTexture0 = actualTexture;
    QueueTodForcedTextureProbeTexture(texture, actualTexture);
    if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyBoxRenderDepth), 0, 0) > 0) {
      QueueTodSkyTexture(texture != nullptr ? texture : actualTexture, actualTexture, 0, 0, 0);
      InterlockedIncrement(const_cast<LONG*>(&g_todSkyScopedTextureQueues));
    }
  }

  const TextureInfo* boundInfo = FindTextureInfo(actualTexture);
  const bool isRenderTargetTexture = boundInfo != nullptr && IsRenderTargetUsage(boundInfo->usage);
  if (count <= 80 || isRenderTargetTexture || (stage == 0 && (count % 1000) == 0)) {
    Log(
        "SetTexture #%ld: hr=0x%08lx stage=%lu tex=%p bound=%p copyBound=%d layoutPreT=%d rtTex=%d "
        "info=%ux%u/%s usage=0x%08lx pool=%u levels=%u preloaded=%d level0=%p",
        count,
        static_cast<unsigned long>(result),
        stage,
        texture,
        actualTexture,
        copiedTextureBound ? 1 : 0,
        preTransformedLayout ? 1 : 0,
        isRenderTargetTexture ? 1 : 0,
        boundInfo != nullptr ? boundInfo->width : 0,
        boundInfo != nullptr ? boundInfo->height : 0,
        boundInfo != nullptr ? FormatName(boundInfo->format) : "unknown",
        boundInfo != nullptr ? boundInfo->usage : 0,
        boundInfo != nullptr ? static_cast<unsigned>(boundInfo->pool) : 0,
        boundInfo != nullptr ? boundInfo->levels : 0,
        preloadCount != 0 ? 1 : 0,
        boundInfo != nullptr ? boundInfo->level0Surface : nullptr);
  }

  return result;
}

HRESULT APIENTRY Hook_StretchRect(
    IDirect3DDevice9* self,
    IDirect3DSurface9* sourceSurface,
    const RECT* sourceRect,
    IDirect3DSurface9* destSurface,
    const RECT* destRect,
    D3DTEXTUREFILTERTYPE filter) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_stretchRectCalls));
  SurfaceInfo* sourceInfo = EnsureSurfaceInfo(sourceSurface, false);
  SurfaceInfo* destInfo = EnsureSurfaceInfo(destSurface, false);
  const HRESULT result = g_origStretchRect(
      self,
      sourceSurface,
      sourceRect,
      destSurface,
      destRect,
      filter);

  if (count <= 100 || (count % 500) == 0) {
    Log(
        "StretchRect #%ld: hr=0x%08lx src=%p %ux%u/%s dst=%p %ux%u/%s filter=%u",
        count,
        static_cast<unsigned long>(result),
        sourceSurface,
        sourceInfo != nullptr ? sourceInfo->width : 0,
        sourceInfo != nullptr ? sourceInfo->height : 0,
        sourceInfo != nullptr ? FormatName(sourceInfo->format) : "unknown",
        destSurface,
        destInfo != nullptr ? destInfo->width : 0,
        destInfo != nullptr ? destInfo->height : 0,
        destInfo != nullptr ? FormatName(destInfo->format) : "unknown",
        static_cast<unsigned>(filter));
  }

  return result;
}

void InjectSunLight(IDirect3DDevice9* device) {
  if (!kInjectSunLight || device == nullptr) {
    return;
  }
  RefreshSunLightConfig();

  D3DLIGHT9 sun = {};
  sun.Type = D3DLIGHT_DIRECTIONAL;
  // Step 1 confirmed Remix ray-traces the injected light. Values are runtime-
  // tunable from scripts/TODCameraResend.ini while we match TOD's sky sun.
  sun.Diffuse.r = g_sunConfig.diffuseR;
  sun.Diffuse.g = g_sunConfig.diffuseG;
  sun.Diffuse.b = g_sunConfig.diffuseB;
  sun.Diffuse.a = 1.0f;
  sun.Specular.r = g_sunConfig.diffuseR * g_sunConfig.specularScale;
  sun.Specular.g = g_sunConfig.diffuseG * g_sunConfig.specularScale;
  sun.Specular.b = g_sunConfig.diffuseB * g_sunConfig.specularScale;
  sun.Specular.a = 1.0f;
  // Prefer the auto-aimed direction (captured from TOD's sun billboard) once it is
  // valid; fall back to the INI Direction* vector until the sun has been seen.
  float dirX = g_sunConfig.directionX;
  float dirY = g_sunConfig.directionY;
  float dirZ = g_sunConfig.directionZ;
  const bool autoAim =
      kEnableSunAutoAim &&
      InterlockedCompareExchange(const_cast<LONG*>(&g_sunAutoAimEnabled), 0, 0) != 0 &&
      InterlockedCompareExchange(const_cast<LONG*>(&g_sunAutoDirValid), 0, 0) != 0;
  if (autoAim) {
    dirX = g_sunAutoDirX;
    dirY = g_sunAutoDirY;
    dirZ = g_sunAutoDirZ;
  }
  sun.Direction.x = dirX;
  sun.Direction.y = dirY;
  sun.Direction.z = dirZ;
  sun.Range = g_sunConfig.range;

  const HRESULT hrSet = device->SetLight(kSunLightIndex, &sun);
  const HRESULT hrEnable = device->LightEnable(kSunLightIndex, TRUE);
  const LONG n = InterlockedIncrement(const_cast<LONG*>(&g_sunLightInjections));
  if (n <= 5 || (n % 600) == 0) {
    Log("InjectSunLight #%ld: SetLight hr=0x%08lx LightEnable hr=0x%08lx dir=(%.2f,%.2f,%.2f)",
        n,
        static_cast<unsigned long>(hrSet),
        static_cast<unsigned long>(hrEnable),
        sun.Direction.x,
        sun.Direction.y,
        sun.Direction.z);
  }
}

// Forward declarations (defined with the billboard helpers further below).
extern volatile LONG g_billboardRemapDraws;
extern volatile LONG g_billboardRemapLockFails;
extern volatile LONG g_billboardCaptureRows;
void WriteBillboardRow(const char* line);

HRESULT APIENTRY Hook_Present(
    IDirect3DDevice9* self,
    const RECT* sourceRect,
    const RECT* destRect,
    HWND destWindowOverride,
    const RGNDATA* dirtyRegion) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_presentCalls));
  InterlockedExchange(const_cast<LONG*>(&g_sunHashAttemptsThisFrame), 0);
  InjectSunLight(self);
  if (kRemapBillboardIndices && count == 300) {
    char s[160] = {};
    snprintf(s, sizeof(s), "-1\tSUMMARY\tremapDraws=%ld\tlockFails=%ld\tcaptureRows=%ld\t-\t-\t-\t-\t-\r\n",
             InterlockedCompareExchange(const_cast<LONG*>(&g_billboardRemapDraws), 0, 0),
             InterlockedCompareExchange(const_cast<LONG*>(&g_billboardRemapLockFails), 0, 0),
             InterlockedCompareExchange(const_cast<LONG*>(&g_billboardCaptureRows), 0, 0));
    WriteBillboardRow(s);
  }
  ApplyTodCameraFarClipOverride();
  ProcessQueuedTodSkyTextures();
  RunTodSkyAssetLoadProbe();
  RunTodTextureMapProbe();
  ProcessQueuedTodForcedTextureProbeTextures();
  if (count <= 5 || (count % 60) == 0) {
    LogSummary("present");
  }

  const HRESULT result = g_origPresent(self, sourceRect, destRect, destWindowOverride, dirtyRegion);
  InterlockedExchange(const_cast<LONG*>(&g_worldDrawnThisFrame), 0);
  return result;
}

HRESULT APIENTRY Hook_SetTransform(
    IDirect3DDevice9* self,
    D3DTRANSFORMSTATETYPE state,
    const D3DMATRIX* matrix) {
  if (matrix != nullptr) {
    if (state == D3DTS_VIEW) {
      const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setViewCalls));
      if (IsIdentityView(*matrix)) {
        InterlockedExchange(const_cast<LONG*>(&g_currentViewIsIdentity), 1);
        const LONG ignored = InterlockedIncrement(const_cast<LONG*>(&g_ignoredIdentityViews));
        if (ignored <= 5 || (ignored % 1000) == 0) {
          Log("ignored identity D3DTS_VIEW #%ld at SetTransform call #%ld", ignored, count);
        }
      } else {
        InterlockedExchange(const_cast<LONG*>(&g_currentViewIsIdentity), 0);
        g_lastView = *matrix;
        InterlockedExchange(const_cast<LONG*>(&g_haveView), 1);
        LogMatrixSample("cached non-identity D3DTS_VIEW", count, g_lastView);
      }
    } else if (state == D3DTS_PROJECTION) {
      g_lastProjection = *matrix;
      InterlockedExchange(const_cast<LONG*>(&g_haveProjection), 1);
      const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setProjectionCalls));
      LogMatrixSample("cached D3DTS_PROJECTION", count, g_lastProjection);
    } else if (state == D3DTS_WORLD) {
      g_lastWorld = *matrix;
      InterlockedExchange(const_cast<LONG*>(&g_haveWorld), 1);
    }
  }

  return g_origSetTransform(self, state, matrix);
}

struct TodFogStateSlot {
  D3DRENDERSTATETYPE state;
  const char* name;
  bool isFloat;
  bool seen;
  DWORD lastValue;
  LONG lastSky;
};

TodFogStateSlot g_todFogSlots[] = {
    {D3DRS_FOGENABLE, "FOGENABLE", false, false, 0, 0},
    {D3DRS_FOGCOLOR, "FOGCOLOR", false, false, 0, 0},
    {D3DRS_FOGTABLEMODE, "FOGTABLEMODE", false, false, 0, 0},
    {D3DRS_FOGSTART, "FOGSTART", true, false, 0, 0},
    {D3DRS_FOGEND, "FOGEND", true, false, 0, 0},
    {D3DRS_FOGDENSITY, "FOGDENSITY", true, false, 0, 0},
    {D3DRS_RANGEFOGENABLE, "RANGEFOGENABLE", false, false, 0, 0},
    {D3DRS_FOGVERTEXMODE, "FOGVERTEXMODE", false, false, 0, 0},
};

volatile LONG g_todFogStateRows = 0;

// Records each distinct fixed-function fog render-state TOD submits, noting
// whether it happens inside SkyBox::Render scope. Deduped on changed value so
// the file stays small. Diagnostic only; behavior is unchanged.
void LogTodFogState(D3DRENDERSTATETYPE state, DWORD value) {
  if (!kEnableTodFogStateLog) {
    return;
  }
  TodFogStateSlot* slot = nullptr;
  for (TodFogStateSlot& candidate : g_todFogSlots) {
    if (candidate.state == state) {
      slot = &candidate;
      break;
    }
  }
  if (slot == nullptr) {
    return;
  }

  const LONG sky =
      InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyBoxRenderDepth), 0, 0) > 0 ? 1 : 0;
  if (slot->seen && slot->lastValue == value && slot->lastSky == sky) {
    return;
  }
  slot->seen = true;
  slot->lastValue = value;
  slot->lastSky = sky;

  const LONG row = InterlockedIncrement(const_cast<LONG*>(&g_todFogStateRows));
  if (row > 20000) {
    return;
  }

  char decoded[128] = {};
  if (state == D3DRS_FOGCOLOR) {
    snprintf(
        decoded,
        sizeof(decoded),
        "A=%lu R=%lu G=%lu B=%lu",
        (value >> 24) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 8) & 0xFF,
        value & 0xFF);
  } else if (slot->isFloat) {
    float f = 0.0f;
    memcpy(&f, &value, sizeof(f));
    snprintf(decoded, sizeof(decoded), "%.4f", f);
  } else {
    snprintf(decoded, sizeof(decoded), "%lu", static_cast<unsigned long>(value));
  }

  char line[512] = {};
  snprintf(
      line,
      sizeof(line),
      "%ld\t%s\t0x%08lx\t%s\t%ld\t%ld\r\n",
      row,
      slot->name,
      static_cast<unsigned long>(value),
      decoded,
      sky,
      InterlockedCompareExchange(const_cast<LONG*>(&g_setRenderStateCalls), 0, 0));

  const char* path =
      g_todFogStateLogPath[0] != '\0' ? g_todFogStateLogPath : "rtx-remix\\logs\\tod-fog-state.tsv";
  const bool newFile = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (newFile) {
      const char* header = "row\tfogState\trawValue\tdecoded\tskyScope\tsetRenderStateCall\r\n";
      WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    }
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

HRESULT APIENTRY Hook_SetRenderState(
    IDirect3DDevice9* self,
    D3DRENDERSTATETYPE state,
    DWORD value) {
  const HRESULT result = g_origSetRenderState(self, state, value);
  if (SUCCEEDED(result)) {
    InterlockedIncrement(const_cast<LONG*>(&g_setRenderStateCalls));
    LogTodFogState(state, value);
    switch (state) {
      case D3DRS_ZENABLE:
        InterlockedExchange(const_cast<LONG*>(&g_zEnable), value != 0 ? 1 : 0);
        break;
      case D3DRS_ZWRITEENABLE:
        InterlockedExchange(const_cast<LONG*>(&g_zWriteEnable), value != 0 ? 1 : 0);
        break;
      case D3DRS_ALPHABLENDENABLE:
        InterlockedExchange(const_cast<LONG*>(&g_alphaBlendEnable), value != 0 ? 1 : 0);
        break;
      case D3DRS_ALPHATESTENABLE:
        InterlockedExchange(const_cast<LONG*>(&g_alphaTestEnable), value != 0 ? 1 : 0);
        break;
      default:
        break;
    }
  }

  return result;
}

HRESULT APIENTRY Hook_SetFVF(IDirect3DDevice9* self, DWORD fvf) {
  g_currentFvf = fvf;
  g_currentDeclaration = nullptr;

  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setFvfCalls));
  if (count <= 20 || (count % 5000) == 0) {
    Log(
        "SetFVF #%ld: fvf=0x%08lx rhw=%d",
        count,
        fvf,
        IsPreTransformedFvf(fvf) ? 1 : 0);
  }

  return g_origSetFVF(self, fvf);
}

HRESULT APIENTRY Hook_CreateVertexDeclaration(
    IDirect3DDevice9* self,
    const D3DVERTEXELEMENT9* vertexElements,
    IDirect3DVertexDeclaration9** declaration) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_createDeclarationCalls));
  const bool logElements = count <= 10;
  bool hasPosition = false;
  bool hasPositionT = false;

  if (logElements) {
    hasPositionT = VertexElementsHaveUsage(
        vertexElements,
        D3DDECLUSAGE_POSITIONT,
        "CreateVertexDeclaration",
        &hasPosition);
  } else {
    hasPositionT = VertexElementsHaveUsage(
        vertexElements,
        D3DDECLUSAGE_POSITIONT,
        nullptr,
        &hasPosition);
  }

  const HRESULT result = g_origCreateVertexDeclaration(self, vertexElements, declaration);

  if (SUCCEEDED(result) && declaration != nullptr && *declaration != nullptr) {
    RegisterDeclaration(*declaration, hasPosition, hasPositionT);
  }

  Log(
      "CreateVertexDeclaration #%ld: hr=0x%08lx decl=%p hasPOSITION=%d hasPOSITIONT=%d",
      count,
      static_cast<unsigned long>(result),
      declaration != nullptr ? *declaration : nullptr,
      hasPosition ? 1 : 0,
      hasPositionT ? 1 : 0);

  return result;
}

HRESULT APIENTRY Hook_SetVertexDeclaration(
    IDirect3DDevice9* self,
    IDirect3DVertexDeclaration9* declaration) {
  g_currentDeclaration = declaration;
  g_currentFvf = 0;

  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setDeclarationCalls));
  const bool hasPositionT = DeclarationHasPositionT(declaration);
  if (count <= 20 || (count % 1000) == 0 || hasPositionT) {
    Log(
        "SetVertexDeclaration #%ld: decl=%p hasPOSITIONT=%d",
        count,
        declaration,
        hasPositionT ? 1 : 0);
  }

  return g_origSetVertexDeclaration(self, declaration);
}

HRESULT APIENTRY Hook_SetVertexShader(
    IDirect3DDevice9* self,
    IDirect3DVertexShader9* shader) {
  g_currentVertexShader = shader;

  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_setVertexShaderCalls));
  if (count <= 20 || (count % 1000) == 0 || shader != nullptr) {
    Log("SetVertexShader #%ld: shader=%p", count, shader);
  }

  return g_origSetVertexShader(self, shader);
}

// Sun auto-aim capture entry points (defined with the helpers before the indexed-draw
// hook); forward-declared so Hook_DrawPrimitive above can call them.
UINT SunVertexCountForPrim(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount);
void CaptureSunFromIndexedDraw(
    IDirect3DDevice9* device,
    const LayoutState& layout,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices);

HRESULT APIENTRY Hook_DrawPrimitive(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    UINT startVertex,
    UINT primitiveCount) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_primitiveDrawCalls));
  const LayoutState layout = GetLayoutState();
  const bool preTransformed = layout.fvfRhw || layout.declarationPositionT;
  if (preTransformed) {
    InterlockedIncrement(const_cast<LONG*>(&g_preTransformedPrimitiveDraws));
  }

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  const bool skipRtComposite =
      kSkipRtTextureCompositesToPrimary && preTransformed && IsRenderTargetTexture(g_currentTexture0) &&
      (kSkipAllRtTextureComposites || IsPrimarySurfaceInfo(rtInfo));
  if (skipRtComposite) {
    const LONG skipped = InterlockedIncrement(const_cast<LONG*>(&g_skippedRtCompositeDraws));
    if (skipped <= 20 || (skipped % 1000) == 0) {
      const TextureInfo* texInfo = FindTextureInfo(g_currentTexture0);
      Log(
          "skipped RT composite DrawPrimitive #%ld skipped=%ld: primType=%u primCount=%u "
          "rt0=%p rt0Info=%ux%u/%s tex0=%p tex0Info=%ux%u/%s",
          count,
          skipped,
          static_cast<unsigned>(primitiveType),
          primitiveCount,
          g_currentRenderTarget0,
          rtInfo != nullptr ? rtInfo->width : 0,
          rtInfo != nullptr ? rtInfo->height : 0,
          rtInfo != nullptr ? FormatName(rtInfo->format) : "unknown",
          g_currentTexture0,
          texInfo != nullptr ? texInfo->width : 0,
          texInfo != nullptr ? texInfo->height : 0,
          texInfo != nullptr ? FormatName(texInfo->format) : "unknown");
    }
    return D3D_OK;
  }

  IDirect3DBaseTexture9* restoreTexture = BindPreTransformedA8CopyForDraw(self, layout);
  LogDrawSample("DrawPrimitive", count, primitiveType, primitiveCount, 0, layout, false);
  CaptureSunFromIndexedDraw(
      self,
      layout,
      static_cast<INT>(startVertex),
      0,
      SunVertexCountForPrim(primitiveType, primitiveCount));

  const HRESULT result = g_origDrawPrimitive(self, primitiveType, startVertex, primitiveCount);
  RestoreTexture0AfterDraw(self, restoreTexture);
  MarkWorldDrawIfPrimary(layout);
  return result;
}

volatile LONG g_billboardCaptureRows = 0;
constexpr LONG kMaxBillboardPatterns = 64;
unsigned long long g_billboardPatternSignatures[kMaxBillboardPatterns] = {};
volatile LONG g_billboardPatternCount = 0;

bool BillboardPatternSeen(unsigned long long sig) {
  const LONG count = ClampedCount(&g_billboardPatternCount, kMaxBillboardPatterns);
  for (LONG i = 0; i < count; ++i) {
    if (g_billboardPatternSignatures[i] == sig) {
      return true;
    }
  }
  const LONG idx = InterlockedIncrement(const_cast<LONG*>(&g_billboardPatternCount)) - 1;
  if (idx >= 0 && idx < kMaxBillboardPatterns) {
    g_billboardPatternSignatures[idx] = sig;
  }
  return false;
}

void WriteBillboardRow(const char* line) {
  const char* path =
      g_todBillboardLogPath[0] != '\0' ? g_todBillboardLogPath : "rtx-remix\\logs\\tod-billboard-indices.tsv";
  const bool newFile = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (newFile) {
      const char* header =
          "row\tblend\tatest\tzwrite\tprimCount\tnumVerts\tindexed\tfmt16\tABCACD\tidx0_11\r\n";
      WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    }
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

// Reads the index buffer of an alpha-blended/tested indexed quad batch (foliage /
// billboard candidate) and logs whether its quad layout is A,B,C,A,C,D (what Remix's
// createBillboards requires) plus the first 12 index values.
void CaptureBillboardIndices(IDirect3DDevice9* device, UINT startIndex, UINT primitiveCount,
                             UINT numVertices) {
  if (!kCaptureBillboardIndices || device == nullptr) {
    return;
  }
  if (InterlockedCompareExchange(const_cast<LONG*>(&g_billboardCaptureRows), 0, 0) >= 300) {
    return;
  }
  const bool blend = InterlockedCompareExchange(const_cast<LONG*>(&g_alphaBlendEnable), 0, 0) != 0;
  const bool atest = InterlockedCompareExchange(const_cast<LONG*>(&g_alphaTestEnable), 0, 0) != 0;
  if (!blend && !atest) {
    return;  // only alpha overlay/cutout draws (foliage/billboards/particles)
  }
  if (primitiveCount < 2 || (primitiveCount % 2) != 0) {
    return;  // quads only (each quad = 2 triangles)
  }
  const bool zwrite = InterlockedCompareExchange(const_cast<LONG*>(&g_zWriteEnable), 0, 0) != 0;

  uint32_t v[12] = {};
  UINT readCount = primitiveCount * 3;
  if (readCount > 12) {
    readCount = 12;
  }
  bool indexed = true;
  bool fmt16 = true;

  IDirect3DIndexBuffer9* ib = nullptr;
  if (FAILED(device->GetIndices(&ib)) || ib == nullptr) {
    indexed = false;
    readCount = 0;
  } else {
    D3DINDEXBUFFER_DESC desc = {};
    ib->GetDesc(&desc);
    fmt16 = desc.Format == D3DFMT_INDEX16;
    const UINT idxSize = fmt16 ? 2u : 4u;
    void* data = nullptr;
    if (SUCCEEDED(ib->Lock(startIndex * idxSize, readCount * idxSize, &data, D3DLOCK_READONLY)) &&
        data != nullptr) {
      for (UINT i = 0; i < readCount; ++i) {
        v[i] = fmt16 ? static_cast<uint32_t>(reinterpret_cast<uint16_t*>(data)[i])
                     : reinterpret_cast<uint32_t*>(data)[i];
      }
      ib->Unlock();
    } else {
      ib->Release();
      return;
    }
    ib->Release();
  }

  // Dedup by the relative pattern of the first quad (and indexed-ness).
  unsigned long long sig = indexed ? 1 : 0;
  for (UINT i = 0; i < 6 && i < readCount; ++i) {
    sig = sig * 131 + static_cast<unsigned long long>(v[i] - v[0]) + 1;
  }
  if (BillboardPatternSeen(sig)) {
    return;
  }

  const bool abcacd = indexed && readCount >= 6 && v[0] == v[3] && v[2] == v[4];
  const LONG row = InterlockedIncrement(const_cast<LONG*>(&g_billboardCaptureRows));

  char idxStr[128] = {};
  int p = 0;
  for (UINT i = 0; i < readCount && p < static_cast<int>(sizeof(idxStr)) - 8; ++i) {
    p += snprintf(idxStr + p, sizeof(idxStr) - p, "%u%s", v[i], (i + 1 < readCount) ? "," : "");
  }

  char line[256] = {};
  snprintf(line, sizeof(line), "%ld\t%d\t%d\t%d\t%u\t%u\t%d\t%d\t%d\t%s\r\n",
           row, blend ? 1 : 0, atest ? 1 : 0, zwrite ? 1 : 0, primitiveCount, numVertices,
           indexed ? 1 : 0, fmt16 ? 1 : 0, abcacd ? 1 : 0, idxStr);
  WriteBillboardRow(line);
}

volatile LONG g_billboardRemapDraws = 0;
volatile LONG g_billboardRemapLockFails = 0;

// Rewrites TOD's quad index layout A,B,C,C,D,A -> A,B,C,A,C,D in place so Remix's
// createBillboards accepts the quads. Same two triangles (same verts/winding), so the
// rasterized result is identical; only Remix's billboard reconstruction is unblocked.
void RemapBillboardIndices(IDirect3DDevice9* device, UINT startIndex, UINT primitiveCount) {
  if (!kRemapBillboardIndices || device == nullptr) {
    return;
  }
  const bool blend = InterlockedCompareExchange(const_cast<LONG*>(&g_alphaBlendEnable), 0, 0) != 0;
  const bool atest = InterlockedCompareExchange(const_cast<LONG*>(&g_alphaTestEnable), 0, 0) != 0;
  if (!blend && !atest) {
    return;  // alpha overlay/cutout draws only (foliage/billboards/particles)
  }
  if (primitiveCount < 2 || (primitiveCount % 2) != 0) {
    return;  // quad batches only
  }

  IDirect3DIndexBuffer9* ib = nullptr;
  if (FAILED(device->GetIndices(&ib)) || ib == nullptr) {
    return;
  }
  D3DINDEXBUFFER_DESC desc = {};
  ib->GetDesc(&desc);
  if (desc.Format != D3DFMT_INDEX16) {
    ib->Release();
    return;  // TOD's billboard quads are 16-bit (confirmed by capture)
  }

  const UINT indexCount = primitiveCount * 3;
  void* data = nullptr;
  if (FAILED(ib->Lock(startIndex * 2u, indexCount * 2u, &data, 0)) || data == nullptr) {
    InterlockedIncrement(const_cast<LONG*>(&g_billboardRemapLockFails));
    ib->Release();
    return;
  }

  uint16_t* idx = reinterpret_cast<uint16_t*>(data);
  for (UINT q = 0; q + 6 <= indexCount; q += 6) {
    // Already in Remix's A,B,C,A,C,D layout? (idempotent for static/already-remapped IBs)
    if (idx[q + 3] == idx[q + 0] && idx[q + 4] == idx[q + 2]) {
      continue;
    }
    // Only remap quads that follow TOD's exact A,B,C,C,D,A invariant (i3==i2, i5==i0);
    // leave any other layout untouched for safety.
    if (idx[q + 3] != idx[q + 2] || idx[q + 5] != idx[q + 0]) {
      continue;
    }
    const uint16_t a = idx[q + 0];
    const uint16_t c = idx[q + 2];
    const uint16_t d = idx[q + 4];
    idx[q + 3] = a;
    idx[q + 4] = c;
    idx[q + 5] = d;
  }

  ib->Unlock();
  ib->Release();
  InterlockedIncrement(const_cast<LONG*>(&g_billboardRemapDraws));
}

void WriteFoliageFreezeRow(
    const char* status,
    IDirect3DVertexBuffer9* original,
    IDirect3DVertexBuffer9* frozen,
    IDirect3DBaseTexture9* texture,
    IDirect3DIndexBuffer9* indexBuffer,
    UINT streamOffset,
    UINT stride,
    UINT firstVertex,
    UINT numVertices,
    UINT startIndex,
    UINT primitiveCount,
    UINT copyBytes) {
  const LONG row = InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldRows));
  if (row > 300 && (status == nullptr || strcmp(status, "SUMMARY") != 0)) {
    return;
  }

  char line[512] = {};
  snprintf(
      line,
      sizeof(line),
      "%ld\t%s\t%p\t%p\t%p\t%p\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%ld\t%ld\t%ld\t%ld\t%ld\r\n",
      row,
      status != nullptr ? status : "unknown",
      original,
      frozen,
      texture,
      indexBuffer,
      streamOffset,
      stride,
      firstVertex,
      numVertices,
      startIndex,
      primitiveCount,
      copyBytes,
      InterlockedCompareExchange(const_cast<LONG*>(&g_frozenAlphaWorldCandidates), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_frozenAlphaWorldCreates), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_frozenAlphaWorldBinds), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_frozenAlphaWorldLockFails), 0, 0),
      InterlockedCompareExchange(const_cast<LONG*>(&g_frozenAlphaWorldCreateFails), 0, 0));

  const char* path = g_todFoliageFreezeLogPath[0] != '\0'
      ? g_todFoliageFreezeLogPath
      : "rtx-remix\\logs\\tod-foliage-freeze.tsv";
  const bool newFile = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;

  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (newFile) {
      const char* header =
          "row\tstatus\toriginalVB\tfrozenVB\ttexture\tindexBuffer\tstreamOffset\tstride"
          "\tfirstVertex\tnumVertices\tstartIndex\tprimitiveCount\tcopyBytes"
          "\tcandidates\tcreates\tbinds\tlockFails\tcreateFails\r\n";
      WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
    }
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

bool IsAnimatedAlphaWorldFreezeCandidate(
    const LayoutState& layout,
    D3DPRIMITIVETYPE primitiveType,
    UINT numVertices,
    UINT primitiveCount) {
  if (!kFreezeAnimatedAlphaWorldVertexBuffers || primitiveType != D3DPT_TRIANGLELIST) {
    return false;
  }
  if (layout.fvfRhw || layout.declarationPositionT || layout.shaderActive || layout.unknown) {
    return false;
  }
  if (primitiveCount < kFreezeAlphaWorldMinPrimitiveCount ||
      numVertices < kFreezeAlphaWorldMinVertexCount) {
    return false;
  }

  const bool alphaBlend =
      InterlockedCompareExchange(const_cast<LONG*>(&g_alphaBlendEnable), 0, 0) != 0;
  const bool alphaTest =
      InterlockedCompareExchange(const_cast<LONG*>(&g_alphaTestEnable), 0, 0) != 0;
  const bool depthEnabled =
      InterlockedCompareExchange(const_cast<LONG*>(&g_zEnable), 0, 0) != 0;
  const bool depthWrite =
      InterlockedCompareExchange(const_cast<LONG*>(&g_zWriteEnable), 0, 0) != 0;
  if ((!alphaBlend && !alphaTest) || !depthEnabled || !depthWrite) {
    return false;
  }

  const SurfaceInfo* rtInfo = FindSurfaceInfo(g_currentRenderTarget0);
  if (!IsPrimarySurfaceInfo(rtInfo)) {
    return false;
  }

  const TextureInfo* texInfo = FindTextureInfo(
      g_currentSourceTexture0 != nullptr ? g_currentSourceTexture0 : g_currentTexture0);
  return texInfo != nullptr && !IsRenderTargetUsage(texInfo->usage);
}

FrozenAlphaWorldVertexBufferRecord* FindFrozenAlphaWorldVertexBuffer(
    IDirect3DVertexBuffer9* original,
    IDirect3DIndexBuffer9* indexBuffer,
    IDirect3DBaseTexture9* texture,
    UINT streamOffset,
    UINT stride,
    UINT firstVertex,
    UINT numVertices,
    UINT startIndex,
    UINT primitiveCount) {
  const LONG count = ClampedCount(
      &g_frozenAlphaWorldRecordCount, kMaxFrozenAlphaWorldVertexBufferRecords);
  for (LONG i = 0; i < count; ++i) {
    FrozenAlphaWorldVertexBufferRecord& record = g_frozenAlphaWorldVertexBuffers[i];
    if (record.valid &&
        record.original == original &&
        record.indexBuffer == indexBuffer &&
        record.texture == texture &&
        record.streamOffset == streamOffset &&
        record.stride == stride &&
        record.firstVertex == firstVertex &&
        record.numVertices == numVertices &&
        record.startIndex == startIndex &&
        record.primitiveCount == primitiveCount) {
      return &record;
    }
  }
  return nullptr;
}

FrozenAlphaWorldVertexBufferRecord* CreateFrozenAlphaWorldVertexBuffer(
    IDirect3DDevice9* device,
    IDirect3DVertexBuffer9* original,
    IDirect3DIndexBuffer9* indexBuffer,
    IDirect3DBaseTexture9* texture,
    UINT streamOffset,
    UINT stride,
    UINT firstVertex,
    UINT numVertices,
    UINT startIndex,
    UINT primitiveCount) {
  if (device == nullptr || original == nullptr || stride == 0 || numVertices == 0) {
    return nullptr;
  }

  const UINT copyBytes = numVertices * stride;
  if (copyBytes == 0 || copyBytes > kFreezeAlphaWorldMaxCopyBytes) {
    return nullptr;
  }

  D3DVERTEXBUFFER_DESC desc = {};
  if (FAILED(original->GetDesc(&desc))) {
    return nullptr;
  }

  const unsigned long long byteOffset =
      static_cast<unsigned long long>(streamOffset) +
      static_cast<unsigned long long>(firstVertex) * static_cast<unsigned long long>(stride);
  const unsigned long long byteEnd = byteOffset + copyBytes;
  if (byteEnd > desc.Size || byteEnd < byteOffset) {
    return nullptr;
  }

  const LONG slot =
      InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldRecordCount)) - 1;
  if (slot < 0 || slot >= kMaxFrozenAlphaWorldVertexBufferRecords) {
    return nullptr;
  }

  IDirect3DVertexBuffer9* frozen = nullptr;
  const HRESULT createHr = device->CreateVertexBuffer(
      copyBytes, 0, 0, D3DPOOL_DEFAULT, &frozen, nullptr);
  if (FAILED(createHr) || frozen == nullptr) {
    InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldCreateFails));
    WriteFoliageFreezeRow(
        "create_failed",
        original,
        nullptr,
        texture,
        indexBuffer,
        streamOffset,
        stride,
        firstVertex,
        numVertices,
        startIndex,
        primitiveCount,
        copyBytes);
    return nullptr;
  }

  void* sourceData = nullptr;
  HRESULT lockSource = original->Lock(
      static_cast<UINT>(byteOffset), copyBytes, &sourceData, D3DLOCK_READONLY);
  if (FAILED(lockSource) || sourceData == nullptr) {
    lockSource = original->Lock(static_cast<UINT>(byteOffset), copyBytes, &sourceData, 0);
  }
  if (FAILED(lockSource) || sourceData == nullptr) {
    frozen->Release();
    InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldLockFails));
    WriteFoliageFreezeRow(
        "source_lock_failed",
        original,
        nullptr,
        texture,
        indexBuffer,
        streamOffset,
        stride,
        firstVertex,
        numVertices,
        startIndex,
        primitiveCount,
        copyBytes);
    return nullptr;
  }

  void* frozenData = nullptr;
  const HRESULT lockFrozen = frozen->Lock(0, copyBytes, &frozenData, 0);
  if (FAILED(lockFrozen) || frozenData == nullptr) {
    original->Unlock();
    frozen->Release();
    InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldLockFails));
    WriteFoliageFreezeRow(
        "frozen_lock_failed",
        original,
        nullptr,
        texture,
        indexBuffer,
        streamOffset,
        stride,
        firstVertex,
        numVertices,
        startIndex,
        primitiveCount,
        copyBytes);
    return nullptr;
  }

  memcpy(frozenData, sourceData, copyBytes);
  frozen->Unlock();
  original->Unlock();

  FrozenAlphaWorldVertexBufferRecord& record = g_frozenAlphaWorldVertexBuffers[slot];
  record.original = original;
  record.indexBuffer = indexBuffer;
  record.texture = texture;
  record.frozen = frozen;
  record.streamOffset = streamOffset;
  record.stride = stride;
  record.firstVertex = firstVertex;
  record.numVertices = numVertices;
  record.startIndex = startIndex;
  record.primitiveCount = primitiveCount;
  record.copyBytes = copyBytes;
  if (record.original != nullptr) {
    record.original->AddRef();
  }
  if (record.indexBuffer != nullptr) {
    record.indexBuffer->AddRef();
  }
  if (record.texture != nullptr) {
    record.texture->AddRef();
  }
  record.valid = true;

  InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldCreates));
  WriteFoliageFreezeRow(
      "created",
      original,
      frozen,
      texture,
      indexBuffer,
      streamOffset,
      stride,
      firstVertex,
      numVertices,
      startIndex,
      primitiveCount,
      copyBytes);
  return &record;
}

FrozenAlphaWorldBind BindFrozenAlphaWorldVertexBufferForDraw(
    IDirect3DDevice9* device,
    const LayoutState& layout,
    D3DPRIMITIVETYPE primitiveType,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    UINT startIndex,
    UINT primitiveCount) {
  FrozenAlphaWorldBind bind = {};
  bind.drawBaseVertexIndex = baseVertexIndex;
  if (!IsAnimatedAlphaWorldFreezeCandidate(layout, primitiveType, numVertices, primitiveCount)) {
    return bind;
  }

  InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldCandidates));

  IDirect3DVertexBuffer9* original = nullptr;
  UINT streamOffset = 0;
  UINT stride = 0;
  if (FAILED(device->GetStreamSource(0, &original, &streamOffset, &stride)) ||
      original == nullptr || stride == 0) {
    return bind;
  }

  IDirect3DIndexBuffer9* indexBuffer = nullptr;
  if (FAILED(device->GetIndices(&indexBuffer)) || indexBuffer == nullptr) {
    original->Release();
    return bind;
  }

  const INT firstVertexSigned = baseVertexIndex + static_cast<INT>(minVertexIndex);
  if (firstVertexSigned < 0) {
    indexBuffer->Release();
    original->Release();
    return bind;
  }

  IDirect3DBaseTexture9* texture =
      g_currentSourceTexture0 != nullptr ? g_currentSourceTexture0 : g_currentTexture0;
  const UINT firstVertex = static_cast<UINT>(firstVertexSigned);
  FrozenAlphaWorldVertexBufferRecord* record = FindFrozenAlphaWorldVertexBuffer(
      original,
      indexBuffer,
      texture,
      streamOffset,
      stride,
      firstVertex,
      numVertices,
      startIndex,
      primitiveCount);
  if (record == nullptr) {
    record = CreateFrozenAlphaWorldVertexBuffer(
        device,
        original,
        indexBuffer,
        texture,
        streamOffset,
        stride,
        firstVertex,
        numVertices,
        startIndex,
        primitiveCount);
  }

  if (record != nullptr && record->valid && record->frozen != nullptr &&
      SUCCEEDED(device->SetStreamSource(0, record->frozen, 0, stride))) {
    bind.original = original;
    bind.streamOffset = streamOffset;
    bind.stride = stride;
    bind.drawBaseVertexIndex = -static_cast<INT>(minVertexIndex);
    bind.applied = true;
    const LONG binds = InterlockedIncrement(const_cast<LONG*>(&g_frozenAlphaWorldBinds));
    if (binds <= 20 || (binds % 300) == 0) {
      WriteFoliageFreezeRow(
          "bound",
          original,
          record->frozen,
          texture,
          indexBuffer,
          streamOffset,
          stride,
          firstVertex,
          numVertices,
          startIndex,
          primitiveCount,
          record->copyBytes);
    }
  } else {
    original->Release();
  }

  indexBuffer->Release();
  return bind;
}

void RestoreFrozenAlphaWorldVertexBuffer(
    IDirect3DDevice9* device,
    const FrozenAlphaWorldBind& bind) {
  if (!bind.applied || bind.original == nullptr || device == nullptr) {
    return;
  }
  device->SetStreamSource(0, bind.original, bind.streamOffset, bind.stride);
  bind.original->Release();
}

// ----------------------------- Sun auto-aim capture -----------------------------
// When TOD draws the lens-flare/sun-disc billboard, read its world-space center and aim
// the injected directional light opposite it. See kEnableSunAutoAim.

struct SunVec3 {
  float x;
  float y;
  float z;
};

// Lightweight always-on status writer (Log() is compiled out in shipping builds, so this
// is how we verify/tune auto-aim). Appends one line to tod-sun-autoaim.log.
void WriteSunAutoAimRow(const char* line) {
  if (line == nullptr) {
    return;
  }
  LockLog();
  EnsureLogDirectory();
  HANDLE file = CreateFileA(
      g_todSunAutoAimLogPath[0] != '\0' ? g_todSunAutoAimLogPath
                                        : "rtx-remix\\logs\\tod-sun-autoaim.log",
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
  }
  UnlockLog();
}

// Row-vector transform (D3D convention v' = v * M), treating p as a point (w = 1).
SunVec3 SunTransformPoint(const D3DMATRIX& m, const SunVec3& p) {
  SunVec3 r;
  r.x = p.x * m._11 + p.y * m._21 + p.z * m._31 + m._41;
  r.y = p.x * m._12 + p.y * m._22 + p.z * m._32 + m._42;
  r.z = p.x * m._13 + p.y * m._23 + p.z * m._33 + m._43;
  return r;
}

// World-space camera position from a rigid view matrix (orthonormal R, translation t):
// eye = -t * R^T.
SunVec3 SunCameraWorldPos(const D3DMATRIX& v) {
  SunVec3 eye;
  eye.x = -(v._41 * v._11 + v._42 * v._12 + v._43 * v._13);
  eye.y = -(v._41 * v._21 + v._42 * v._22 + v._43 * v._23);
  eye.z = -(v._41 * v._31 + v._42 * v._32 + v._43 * v._33);
  return eye;
}

bool SunPtrAlreadyChecked(IDirect3DBaseTexture9* texture) {
  const LONG n = ClampedCount(&g_sunCheckedCount, 512);
  for (LONG i = 0; i < n; ++i) {
    if (g_sunCheckedPtrs[i] == texture) {
      return true;
    }
  }
  return false;
}

void SunMarkChecked(IDirect3DBaseTexture9* texture) {
  const LONG idx = InterlockedIncrement(const_cast<LONG*>(&g_sunCheckedCount)) - 1;
  if (idx >= 0 && idx < 512) {
    g_sunCheckedPtrs[idx] = texture;
  }
}

// True if the currently bound texture is TOD's sun sprite. Resolves it lazily by Remix
// hash (config-supplied) with a small per-frame budget; once found it is a pointer test.
bool IsBoundSunTexture(IDirect3DBaseTexture9* texture) {
  if (!kEnableSunAutoAim || texture == nullptr || g_sunSpriteHash == 0) {
    return false;
  }
  if (texture == g_sunSpriteTexture) {
    return true;
  }
  if (g_sunSpriteTexture != nullptr) {
    return false;  // a different sun sprite is already resolved
  }
  if (texture->GetType() != D3DRTYPE_TEXTURE || SunPtrAlreadyChecked(texture)) {
    return false;
  }
  if (InterlockedIncrement(const_cast<LONG*>(&g_sunHashAttemptsThisFrame)) >
      kSunHashAttemptsPerFrame) {
    return false;
  }
  const TextureInfo* info = FindTextureInfo(texture);
  if (info == nullptr) {
    return false;
  }
  // The flare/sun sprite is small; skip large world textures to bound hashing cost.
  if (info->width == 0 || info->height == 0 || info->width > 256 || info->height > 256) {
    SunMarkChecked(texture);
    return false;
  }
  unsigned long long hash = 0;
  if (!ComputeRtxTextureHash(texture, info, &hash)) {
    SunMarkChecked(texture);
    return false;
  }
  if (hash == g_sunSpriteHash) {
    g_sunSpriteTexture = texture;
    char row[160] = {};
    snprintf(
        row,
        sizeof(row),
        "RESOLVED sun sprite tex=%p hash=0x%016llX size=%ux%u\r\n",
        reinterpret_cast<void*>(texture),
        hash,
        info->width,
        info->height);
    WriteSunAutoAimRow(row);
    return true;
  }
  SunMarkChecked(texture);
  return false;
}

// Aim the injected light from an object-space billboard center.
void UpdateSunDirectionFromObjectCenter(const SunVec3& centerObj) {
  if (InterlockedCompareExchange(const_cast<LONG*>(&g_haveView), 0, 0) == 0) {
    return;
  }
  SunVec3 centerWorld = centerObj;
  if (InterlockedCompareExchange(const_cast<LONG*>(&g_haveWorld), 0, 0) != 0) {
    centerWorld = SunTransformPoint(g_lastWorld, centerObj);
  }
  const SunVec3 eye = SunCameraWorldPos(g_lastView);
  const SunVec3 toSun = {
      centerWorld.x - eye.x, centerWorld.y - eye.y, centerWorld.z - eye.z};
  const float lengthSq = toSun.x * toSun.x + toSun.y * toSun.y + toSun.z * toSun.z;
  if (!IsFiniteFloat(lengthSq) || lengthSq < 1.0e-6f) {
    return;
  }
  const float invLength = 1.0f / sqrtf(lengthSq);
  // Directional light travels FROM the sun toward the scene = -direction-to-sun.
  const float lx = -toSun.x * invLength;
  const float ly = -toSun.y * invLength;
  const float lz = -toSun.z * invLength;
  if (!IsFiniteFloat(lx) || !IsFiniteFloat(ly) || !IsFiniteFloat(lz)) {
    return;
  }
  g_sunAutoDirX = lx;
  g_sunAutoDirY = ly;
  g_sunAutoDirZ = lz;
  InterlockedExchange(const_cast<LONG*>(&g_sunAutoDirValid), 1);
  const LONG n = InterlockedIncrement(const_cast<LONG*>(&g_sunAutoCaptures));
  if (n <= 10 || (n % 120) == 0) {
    char row[256] = {};
    snprintf(
        row,
        sizeof(row),
        "CAPTURE #%ld center=(%.1f,%.1f,%.1f) eye=(%.1f,%.1f,%.1f) lightDir=(%.3f,%.3f,%.3f)\r\n",
        n,
        centerWorld.x,
        centerWorld.y,
        centerWorld.z,
        eye.x,
        eye.y,
        eye.z,
        lx,
        ly,
        lz);
    WriteSunAutoAimRow(row);
  }
}

// Average the position (first 3 floats of each vertex) of a draw's vertices read from
// the bound vertex buffer. Returns the centroid in object space.
bool ReadVbSunCenter(
    IDirect3DDevice9* device,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    SunVec3* outCenter) {
  if (device == nullptr || numVertices == 0 || numVertices > 64) {
    return false;
  }
  IDirect3DVertexBuffer9* vb = nullptr;
  UINT streamOffset = 0;
  UINT stride = 0;
  if (FAILED(device->GetStreamSource(0, &vb, &streamOffset, &stride)) || vb == nullptr ||
      stride < 12) {
    if (vb != nullptr) {
      vb->Release();
    }
    return false;
  }
  const INT firstSigned = baseVertexIndex + static_cast<INT>(minVertexIndex);
  if (firstSigned < 0) {
    vb->Release();
    return false;
  }
  const UINT byteOffset = streamOffset + static_cast<UINT>(firstSigned) * stride;
  const UINT byteCount = numVertices * stride;
  BYTE* data = nullptr;
  if (FAILED(vb->Lock(byteOffset, byteCount, reinterpret_cast<void**>(&data), D3DLOCK_READONLY)) ||
      data == nullptr) {
    vb->Release();
    return false;
  }
  double sx = 0.0;
  double sy = 0.0;
  double sz = 0.0;
  for (UINT i = 0; i < numVertices; ++i) {
    const float* p = reinterpret_cast<const float*>(data + i * stride);
    sx += p[0];
    sy += p[1];
    sz += p[2];
  }
  vb->Unlock();
  vb->Release();
  const float inv = 1.0f / static_cast<float>(numVertices);
  outCenter->x = static_cast<float>(sx) * inv;
  outCenter->y = static_cast<float>(sy) * inv;
  outCenter->z = static_cast<float>(sz) * inv;
  return IsFiniteFloat(outCenter->x) && IsFiniteFloat(outCenter->y) &&
         IsFiniteFloat(outCenter->z);
}

// Same, but for DrawPrimitiveUP/DrawIndexedPrimitiveUP user-pointer vertex data.
bool ReadMemSunCenter(
    const void* vertexData,
    UINT stride,
    UINT numVertices,
    SunVec3* outCenter) {
  if (vertexData == nullptr || stride < 12 || numVertices == 0 || numVertices > 64) {
    return false;
  }
  const BYTE* data = reinterpret_cast<const BYTE*>(vertexData);
  double sx = 0.0;
  double sy = 0.0;
  double sz = 0.0;
  for (UINT i = 0; i < numVertices; ++i) {
    const float* p = reinterpret_cast<const float*>(data + i * stride);
    sx += p[0];
    sy += p[1];
    sz += p[2];
  }
  const float inv = 1.0f / static_cast<float>(numVertices);
  outCenter->x = static_cast<float>(sx) * inv;
  outCenter->y = static_cast<float>(sy) * inv;
  outCenter->z = static_cast<float>(sz) * inv;
  return IsFiniteFloat(outCenter->x) && IsFiniteFloat(outCenter->y) &&
         IsFiniteFloat(outCenter->z);
}

// Non-indexed vertex count for a primitive type/count.
UINT SunVertexCountForPrim(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount) {
  switch (primitiveType) {
    case D3DPT_TRIANGLELIST:
      return primitiveCount * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
      return primitiveCount + 2;
    default:
      return 0;
  }
}

// Object-space capture is only valid for non-pre-transformed fixed-function draws.
bool SunLayoutIsObjectSpace(const LayoutState& layout) {
  return !layout.fvfRhw && !layout.declarationPositionT && !layout.shaderActive;
}

void CaptureSunFromIndexedDraw(
    IDirect3DDevice9* device,
    const LayoutState& layout,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices) {
  if (!kEnableSunAutoAim || !SunLayoutIsObjectSpace(layout)) {
    return;
  }
  IDirect3DBaseTexture9* texture =
      g_currentSourceTexture0 != nullptr ? g_currentSourceTexture0 : g_currentTexture0;
  if (!IsBoundSunTexture(texture)) {
    return;
  }
  SunVec3 center;
  if (ReadVbSunCenter(device, baseVertexIndex, minVertexIndex, numVertices, &center)) {
    UpdateSunDirectionFromObjectCenter(center);
  }
}

void CaptureSunFromUpDraw(
    const LayoutState& layout,
    const void* vertexData,
    UINT stride,
    UINT numVertices) {
  if (!kEnableSunAutoAim || !SunLayoutIsObjectSpace(layout)) {
    return;
  }
  IDirect3DBaseTexture9* texture =
      g_currentSourceTexture0 != nullptr ? g_currentSourceTexture0 : g_currentTexture0;
  if (!IsBoundSunTexture(texture)) {
    return;
  }
  SunVec3 center;
  if (ReadMemSunCenter(vertexData, stride, numVertices, &center)) {
    UpdateSunDirectionFromObjectCenter(center);
  }
}

HRESULT APIENTRY Hook_DrawIndexedPrimitive(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    UINT startIndex,
    UINT primitiveCount) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_indexedDrawCalls));
  const LayoutState layout = GetLayoutState();
  const bool preTransformed = layout.fvfRhw || layout.declarationPositionT;

  if (preTransformed) {
    InterlockedIncrement(const_cast<LONG*>(&g_preTransformedIndexedDraws));
  } else if (layout.shaderActive) {
    InterlockedIncrement(const_cast<LONG*>(&g_shaderIndexedDraws));
  } else if (layout.unknown) {
    InterlockedIncrement(const_cast<LONG*>(&g_unknownIndexedDraws));
  } else {
    InterlockedIncrement(const_cast<LONG*>(&g_fixedFunctionIndexedDraws));
  }

  CaptureSunFromIndexedDraw(self, layout, baseVertexIndex, minVertexIndex, numVertices);

  const bool resent = ResendCameraForDraw(self, preTransformed || layout.shaderActive);
  IDirect3DBaseTexture9* restoreTexture = BindPreTransformedA8CopyForDraw(self, layout);
  FrozenAlphaWorldBind frozenAlphaWorldBind = BindFrozenAlphaWorldVertexBufferForDraw(
      self,
      layout,
      primitiveType,
      baseVertexIndex,
      minVertexIndex,
      numVertices,
      startIndex,
      primitiveCount);
  if (primitiveType == D3DPT_TRIANGLELIST && !preTransformed && !layout.shaderActive) {
    RemapBillboardIndices(self, startIndex, primitiveCount);
    CaptureBillboardIndices(self, startIndex, primitiveCount, numVertices);  // post-remap layout
  }
  const bool todSkyMeshExecution =
      InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyMeshDrawDepth), 0, 0) > 0;
  const bool todSkyDraw =
      todSkyMeshExecution ||
      (primitiveType == D3DPT_TRIANGLELIST && CurrentDrawUsesTodSkyMesh(self, layout));
  D3DVIEWPORT9 savedSkyViewport = {};
  const bool skyViewportMarked =
      todSkyDraw && ApplyTodSkyViewportMarker(self, &savedSkyViewport);
  if (todSkyDraw) {
    const LONG skyDraws = InterlockedIncrement(const_cast<LONG*>(&g_todSkyDraws));
    QueueTodSkyTextureForDraw(skyDraws, primitiveCount, numVertices);
    if (InterlockedCompareExchange(const_cast<LONG*>(&g_todSkyViewportFirstLogged), 1, 0) == 0) {
      IDirect3DBaseTexture9* hashTexture =
          g_currentSourceTexture0 != nullptr ? g_currentSourceTexture0 : g_currentTexture0;
      const TextureInfo* info = FindTextureInfo(hashTexture);
      AppendTodSkyTextureRow(
          skyViewportMarked ? "sky_viewport_marked" : "sky_viewport_not_marked",
          g_currentSourceTexture0,
          g_currentTexture0,
          info,
          0,
          false,
          skyDraws,
          primitiveCount,
          numVertices);
    }
    if (skyDraws <= 10 || (skyDraws % 300) == 0) {
      Log(
          "TOD SkyBox indexed draw #%ld: primCount=%u numVertices=%u viewportMarked=%d meshScope=%d",
          skyDraws,
          primitiveCount,
          numVertices,
          skyViewportMarked ? 1 : 0,
          todSkyMeshExecution ? 1 : 0);
    }
  }
  LogDrawSample("DrawIndexedPrimitive", count, primitiveType, primitiveCount, numVertices, layout, resent);

  const HRESULT result = g_origDrawIndexedPrimitive(
      self,
      primitiveType,
      frozenAlphaWorldBind.drawBaseVertexIndex,
      minVertexIndex,
      numVertices,
      startIndex,
      primitiveCount);
  RestoreFrozenAlphaWorldVertexBuffer(self, frozenAlphaWorldBind);
  if (skyViewportMarked) {
    self->SetViewport(&savedSkyViewport);
  }
  RestoreTexture0AfterDraw(self, restoreTexture);
  MarkWorldDrawIfPrimary(layout);
  return result;
}

HRESULT APIENTRY Hook_DrawPrimitiveUP(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    UINT primitiveCount,
    const void* vertexStreamZeroData,
    UINT vertexStreamZeroStride) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_primitiveUpDrawCalls));
  const LayoutState layout = GetLayoutState();
  if (layout.fvfRhw || layout.declarationPositionT) {
    InterlockedIncrement(const_cast<LONG*>(&g_preTransformedUpDraws));
  }
  IDirect3DBaseTexture9* restoreTexture = BindPreTransformedA8CopyForDraw(self, layout);
  LogDrawSample("DrawPrimitiveUP", count, primitiveType, primitiveCount, 0, layout, false);
  CaptureSunFromUpDraw(
      layout,
      vertexStreamZeroData,
      vertexStreamZeroStride,
      SunVertexCountForPrim(primitiveType, primitiveCount));

  const HRESULT result = g_origDrawPrimitiveUP(
      self,
      primitiveType,
      primitiveCount,
      vertexStreamZeroData,
      vertexStreamZeroStride);
  RestoreTexture0AfterDraw(self, restoreTexture);
  MarkWorldDrawIfPrimary(layout);
  return result;
}

HRESULT APIENTRY Hook_DrawIndexedPrimitiveUP(
    IDirect3DDevice9* self,
    D3DPRIMITIVETYPE primitiveType,
    UINT minVertexIndex,
    UINT numVertices,
    UINT primitiveCount,
    const void* indexData,
    D3DFORMAT indexDataFormat,
    const void* vertexStreamZeroData,
    UINT vertexStreamZeroStride) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_indexedUpDrawCalls));
  const LayoutState layout = GetLayoutState();
  const bool preTransformed = layout.fvfRhw || layout.declarationPositionT;
  if (preTransformed) {
    InterlockedIncrement(const_cast<LONG*>(&g_preTransformedUpDraws));
  }

  const bool resent = ResendCameraForDraw(self, preTransformed || layout.shaderActive);
  IDirect3DBaseTexture9* restoreTexture = BindPreTransformedA8CopyForDraw(self, layout);
  LogDrawSample("DrawIndexedPrimitiveUP", count, primitiveType, primitiveCount, numVertices, layout, resent);
  CaptureSunFromUpDraw(layout, vertexStreamZeroData, vertexStreamZeroStride, numVertices);

  const HRESULT result = g_origDrawIndexedPrimitiveUP(
      self,
      primitiveType,
      minVertexIndex,
      numVertices,
      primitiveCount,
      indexData,
      indexDataFormat,
      vertexStreamZeroData,
      vertexStreamZeroStride);
  RestoreTexture0AfterDraw(self, restoreTexture);
  MarkWorldDrawIfPrimary(layout);
  return result;
}

void HookDevice(IDirect3DDevice9* device) {
  if (device == nullptr) {
    return;
  }

  if (InterlockedExchange(const_cast<LONG*>(&g_deviceHooked), 1) == 0) {
    Log("installing IDirect3DDevice9 hooks on device=%p", device);
  }

  InstallTodSkyBoxRenderHook();
  InstallTodRenderListSetMaterialHook();
  InstallTodRenderListAddMeshHook();
  InstallTodRenderMeshDrawHook();

  HookVtableSlot(device, kDevicePresentIndex, Hook_Present, &g_origPresent, "IDirect3DDevice9::Present");
  HookVtableSlot(
      device,
      kDeviceCreateTextureIndex,
      Hook_CreateTexture,
      &g_origCreateTexture,
      "IDirect3DDevice9::CreateTexture");
  HookVtableSlot(
      device,
      kDeviceCreateRenderTargetIndex,
      Hook_CreateRenderTarget,
      &g_origCreateRenderTarget,
      "IDirect3DDevice9::CreateRenderTarget");
  HookVtableSlot(
      device,
      kDeviceStretchRectIndex,
      Hook_StretchRect,
      &g_origStretchRect,
      "IDirect3DDevice9::StretchRect");
  HookVtableSlot(
      device,
      kDeviceSetRenderTargetIndex,
      Hook_SetRenderTarget,
      &g_origSetRenderTarget,
      "IDirect3DDevice9::SetRenderTarget");
  HookVtableSlot(
      device,
      kDeviceSetDepthStencilSurfaceIndex,
      Hook_SetDepthStencilSurface,
      &g_origSetDepthStencilSurface,
      "IDirect3DDevice9::SetDepthStencilSurface");
  HookVtableSlot(
      device,
      kDeviceSetTransformIndex,
      Hook_SetTransform,
      &g_origSetTransform,
      "IDirect3DDevice9::SetTransform");
  HookVtableSlot(
      device,
      kDeviceSetRenderStateIndex,
      Hook_SetRenderState,
      &g_origSetRenderState,
      "IDirect3DDevice9::SetRenderState");
  HookVtableSlot(device, kDeviceSetTextureIndex, Hook_SetTexture, &g_origSetTexture, "IDirect3DDevice9::SetTexture");
  HookVtableSlot(
      device,
      kDeviceDrawPrimitiveIndex,
      Hook_DrawPrimitive,
      &g_origDrawPrimitive,
      "IDirect3DDevice9::DrawPrimitive");
  HookVtableSlot(
      device,
      kDeviceDrawIndexedPrimitiveIndex,
      Hook_DrawIndexedPrimitive,
      &g_origDrawIndexedPrimitive,
      "IDirect3DDevice9::DrawIndexedPrimitive");
  HookVtableSlot(
      device,
      kDeviceDrawPrimitiveUPIndex,
      Hook_DrawPrimitiveUP,
      &g_origDrawPrimitiveUP,
      "IDirect3DDevice9::DrawPrimitiveUP");
  HookVtableSlot(
      device,
      kDeviceDrawIndexedPrimitiveUPIndex,
      Hook_DrawIndexedPrimitiveUP,
      &g_origDrawIndexedPrimitiveUP,
      "IDirect3DDevice9::DrawIndexedPrimitiveUP");
  HookVtableSlot(
      device,
      kDeviceCreateVertexDeclarationIndex,
      Hook_CreateVertexDeclaration,
      &g_origCreateVertexDeclaration,
      "IDirect3DDevice9::CreateVertexDeclaration");
  HookVtableSlot(
      device,
      kDeviceSetVertexDeclarationIndex,
      Hook_SetVertexDeclaration,
      &g_origSetVertexDeclaration,
      "IDirect3DDevice9::SetVertexDeclaration");
  HookVtableSlot(device, kDeviceSetFVFIndex, Hook_SetFVF, &g_origSetFVF, "IDirect3DDevice9::SetFVF");
  HookVtableSlot(
      device,
      kDeviceSetVertexShaderIndex,
      Hook_SetVertexShader,
      &g_origSetVertexShader,
      "IDirect3DDevice9::SetVertexShader");

  IDirect3DSurface9* currentRt = nullptr;
  if (SUCCEEDED(device->GetRenderTarget(0, &currentRt)) && currentRt != nullptr) {
    g_currentRenderTarget0 = currentRt;
    g_primaryRenderTarget = currentRt;
    SurfaceInfo* info = EnsureSurfaceInfo(currentRt, false);
    Log(
        "initial GetRenderTarget(0): surface=%p info=%ux%u/%s usage=0x%08lx",
        currentRt,
        info != nullptr ? info->width : 0,
        info != nullptr ? info->height : 0,
        info != nullptr ? FormatName(info->format) : "unknown",
        info != nullptr ? info->usage : 0);
    currentRt->Release();
  }

  IDirect3DSurface9* currentDepth = nullptr;
  if (SUCCEEDED(device->GetDepthStencilSurface(&currentDepth)) && currentDepth != nullptr) {
    g_currentDepthStencil = currentDepth;
    SurfaceInfo* info = EnsureSurfaceInfo(currentDepth, false);
    Log(
        "initial GetDepthStencilSurface: surface=%p info=%ux%u/%s usage=0x%08lx",
        currentDepth,
        info != nullptr ? info->width : 0,
        info != nullptr ? info->height : 0,
        info != nullptr ? FormatName(info->format) : "unknown",
        info != nullptr ? info->usage : 0);
    currentDepth->Release();
  }
}

HRESULT APIENTRY Hook_CreateDevice(
    IDirect3D9* self,
    UINT adapter,
    D3DDEVTYPE deviceType,
    HWND focusWindow,
    DWORD behaviorFlags,
    D3DPRESENT_PARAMETERS* presentationParameters,
    IDirect3DDevice9** returnedDevice) {
  const LONG count = InterlockedIncrement(const_cast<LONG*>(&g_createDeviceCalls));
  Log(
      "IDirect3D9::CreateDevice #%ld: adapter=%u type=%u flags=0x%08lx",
      count,
      adapter,
      static_cast<unsigned>(deviceType),
      behaviorFlags);

  const HRESULT result = g_origCreateDevice(
      self,
      adapter,
      deviceType,
      focusWindow,
      behaviorFlags,
      presentationParameters,
      returnedDevice);

  Log(
      "IDirect3D9::CreateDevice #%ld returned hr=0x%08lx device=%p",
      count,
      static_cast<unsigned long>(result),
      returnedDevice != nullptr ? *returnedDevice : nullptr);

  if (SUCCEEDED(result) && returnedDevice != nullptr && *returnedDevice != nullptr) {
    HookDevice(*returnedDevice);
  }

  return result;
}

bool HookD3D9Factory(IDirect3D9* d3d9) {
  return HookVtableSlot(
      d3d9,
      kD3D9CreateDeviceIndex,
      Hook_CreateDevice,
      &g_origCreateDevice,
      "IDirect3D9::CreateDevice");
}

DWORD WINAPI WorkerThread(void*) {
  Log("TODCameraResend ASI loaded; gameRoot=%s log=%s", g_gameRoot, g_logPath);

  bool loggedCreateFailure = false;
  for (int attempt = 0; attempt < 3000; ++attempt) {
    HMODULE d3d9Module = GetModuleHandleA("d3d9.dll");
    if (d3d9Module != nullptr) {
      Direct3DCreate9Fn createD3D9 = reinterpret_cast<Direct3DCreate9Fn>(
          GetProcAddress(d3d9Module, "Direct3DCreate9"));

      if (createD3D9 != nullptr) {
        IDirect3D9* d3d9 = createD3D9(D3D_SDK_VERSION);
        if (d3d9 != nullptr) {
          Log("Direct3DCreate9 probe succeeded: d3d9=%p module=%p", d3d9, d3d9Module);
          HookD3D9Factory(d3d9);
          d3d9->Release();
          Log("factory hook ready");
          return 0;
        }

        if (!loggedCreateFailure) {
          Log("Direct3DCreate9 probe returned null; will retry");
          loggedCreateFailure = true;
        }
      }
    }

    Sleep(10);
  }

  Log("timed out waiting for a hookable Direct3D9 factory");
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    InitializePaths(module);
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }

  return TRUE;
}
