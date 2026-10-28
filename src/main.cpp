#include "Common.h"
#include "App.h"
#include "Util.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    Glow::Log(L"[GlowOrb] ==================== launching ====================");
    App app;
    if (!app.Init(hInstance)) return 1;
    return app.Run();
}
