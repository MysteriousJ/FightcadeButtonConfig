#include <Windows.h>
#include <Dbt.h>
#include <stdio.h>
#define JFBJOY_DINPUT
#define JFBJOY_IMPLEMENTATION
#include "jfb_joystick.h"

#define forloop(i,end) for(unsigned int i=0; i<(end); i++)
typedef unsigned int uint;

bool inputPressed(Joystick* joysticks, uint joystickCount, uint* out_inputCode)
{
	forloop(joystickIndex, joystickCount)
	{
		Joystick& joystick = joysticks[joystickIndex];
		uint playerCode = joystickIndex * 0x100;

		forloop(buttonIndex, joystick.maxButtons)
		{
			if (joystick.buttons[buttonIndex].pressed)
			{
				uint buttonCode = 0x80 + buttonIndex;
				*out_inputCode = playerCode + buttonCode;
				return true;
			}
		}

		if (joystick.axes[1].current < -0.5f && joystick.axes[1].previous > -0.5f) {*out_inputCode = playerCode + 2; return true;}
		if (joystick.axes[1].current > 0.5f  && joystick.axes[1].previous < 0.5f)  {*out_inputCode = playerCode + 3; return true;}
		if (joystick.axes[0].current < -0.5f && joystick.axes[0].previous > -0.5f) {*out_inputCode = playerCode + 0; return true;}
		if (joystick.axes[0].current > 0.5f  && joystick.axes[0].previous < 0.5f)  {*out_inputCode = playerCode + 1; return true;}
		if (joystick.axes[2].current < -0.5f && joystick.axes[2].previous > -0.5f) {*out_inputCode = playerCode + 4; return true;}
		if (joystick.axes[2].current > 0.5f  && joystick.axes[2].previous < 0.5f)  {*out_inputCode = playerCode + 5; return true;}

		if (joystick.hat & Hat_up    && !(joystick.previousHat & Hat_up))    {*out_inputCode = playerCode + 0x12; return true;}
		if (joystick.hat & Hat_down  && !(joystick.previousHat & Hat_down))  {*out_inputCode = playerCode + 0x13; return true;}
		if (joystick.hat & Hat_left  && !(joystick.previousHat & Hat_left))  {*out_inputCode = playerCode + 0x10; return true;}
		if (joystick.hat & Hat_right && !(joystick.previousHat & Hat_right)) {*out_inputCode = playerCode + 0x11; return true;}
	}
	return false;
}

void outputButtonMapping(uint inputCode)
{
	// Select previous mapping
	keybd_event(VK_SHIFT, 0, 0, NULL);
	keybd_event(VK_LEFT, 0, 0, NULL);
	keybd_event(VK_LEFT, 0, KEYEVENTF_KEYUP, NULL);
	keybd_event(VK_LEFT, 0, 0, NULL);
	keybd_event(VK_LEFT, 0, KEYEVENTF_KEYUP, NULL);
	keybd_event(VK_LEFT, 0, 0, NULL);
	keybd_event(VK_LEFT, 0, KEYEVENTF_KEYUP, NULL);
	keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, NULL);

	// Type in new mapping
	char buffer[4];
	sprintf_s(buffer, "%03x", inputCode);
	keybd_event(buffer[0], 0, 0, NULL);
	keybd_event(buffer[0], 0, KEYEVENTF_KEYUP, NULL);
	keybd_event(buffer[1], 0, 0, NULL);
	keybd_event(buffer[1], 0, KEYEVENTF_KEYUP, NULL);
	keybd_event(buffer[2], 0, 0, NULL);
	keybd_event(buffer[2], 0, KEYEVENTF_KEYUP, NULL);

	// Move cursor to next line
	keybd_event(VK_DOWN, 0, 0, NULL);
	keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, NULL);
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

HWND createWindow()
{
	WNDCLASS wnd = { 0 };
	wnd.hInstance = GetModuleHandle(0);
	wnd.lpfnWndProc = WindowProcedure;
	wnd.lpszClassName = TEXT("Fightcade Button Config");
	wnd.hCursor = LoadCursor(0, IDC_ARROW);
	RegisterClass(&wnd);
	int width = 300;
	int height = 200;
	HWND hwnd = CreateWindow(wnd.lpszClassName, TEXT("Fightcade Button Config"), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, wnd.hInstance, 0);

	// Setup device context for rendering
	PIXELFORMATDESCRIPTOR requestedFormat = { 0 };
	requestedFormat.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	requestedFormat.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	HDC deviceContext = GetDC(hwnd);
	int actualFormat = ChoosePixelFormat(deviceContext, &requestedFormat);
	SetPixelFormat(deviceContext, actualFormat, &requestedFormat);
	HGLRC renderingContext = wglCreateContext(deviceContext);
	wglMakeCurrent(deviceContext, renderingContext);

	// Register window to be notified when joysticks are plugged in or taken out
	DEV_BROADCAST_DEVICEINTERFACE notificationFilter = { sizeof(DEV_BROADCAST_DEVICEINTERFACE), DBT_DEVTYP_DEVICEINTERFACE };
	RegisterDeviceNotification(0, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

	return hwnd;
}

uint global_joystickCount = 0;
Joystick* global_joysticks = 0;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow)
{
	HWND window = createWindow();
	global_joysticks = createJoysticks(&global_joystickCount);
	bool run = true;
	while (run) 
	{
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT) {
				run = false;
			}
		}

		updateJoysticks(global_joysticks, global_joystickCount);
		uint inputCode = 0;
		if (inputPressed(global_joysticks, global_joystickCount, &inputCode)) {
			outputButtonMapping(inputCode);
		}
		
		// Swap buffers to align main loop with vsynch
		HDC deviceContext = GetDC(window);
		SwapBuffers(deviceContext);
		ReleaseDC(window, deviceContext);
	}
	return 0;
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DEVICECHANGE) {
		destroyJoysticks(global_joysticks, global_joystickCount);
		global_joysticks = createJoysticks(&global_joystickCount);
	}
	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}