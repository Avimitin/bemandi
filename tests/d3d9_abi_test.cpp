#define CINTERFACE
#include "d3d9_abi.h"
#include <cstddef>
using namespace bemandi::d3d9_abi;
static_assert(offsetof(IDirect3D9Vtbl, CreateDevice) / sizeof(void*) == create_device);
static_assert(offsetof(IDirect3D9ExVtbl, CreateDeviceEx) / sizeof(void*) == create_device_ex);
static_assert(offsetof(IDirect3DDevice9Vtbl, Present) / sizeof(void*) == present);
static_assert(offsetof(IDirect3DDevice9ExVtbl, PresentEx) / sizeof(void*) == present_ex);
static_assert(offsetof(IDirect3DDevice9Vtbl, Reset) / sizeof(void*) == reset);
static_assert(offsetof(IDirect3DDevice9ExVtbl, ResetEx) / sizeof(void*) == reset_ex);
static_assert(offsetof(IDirect3DSwapChain9Vtbl, Present) / sizeof(void*) == swap_present);
int main() {}
