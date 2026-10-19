#pragma once
#ifdef BAGUA_HSC_NATIVE_EXPORT
#define BH_API __declspec(dllexport)
#else
#define BH_API __declspec(dllimport)
#endif
// C ABI isolates Qt/C++17 from the C++20 physics SDK. UTF-8 JSON is owned by the caller.
extern "C" {
BH_API unsigned bagua_hsc_native_abi();
BH_API void *bagua_hsc_native_create(const char *scene,const char *workspace,char *error,int errorSize);
BH_API bool bagua_hsc_native_step(void *world,int count,char *error,int errorSize);
BH_API const char *bagua_hsc_native_state(void *world,char *error,int errorSize);
BH_API void bagua_hsc_native_destroy(void *world);
}
