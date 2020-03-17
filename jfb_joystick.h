/* jfb_joystick v0.4 - MIT License
*	
*	In exacly one source file, add
*		#define JFBJOY_IMPLEMENTATION
*	before you #include this header to create the implementation.
*	
*	Every time you #include this file, #define the backends to use.
*		JFBJOY_DINPUT
*			Uses Direct Input on Windows. Supports all controllers.
*			You will need to link with dinput8.lib and dxguid.lib.
*			Shoulder triggers on XBox controllers will share an axis.
*		JFBJOY_XINPUT
*			Uses XInput on Windows. Supports only XBox controllers.
*			You will need to link with Xinput.lib. Earlier versions of Windows
*			may require providing an XInput DLL along with your exe.
*			Properly uses separate axes for the shoulder triggers.
*			Use JFBJOY_DINPUT and JFBJOY_XINPUT together for best results.
*		JFBJOY_SDL
*			Uses the SDL library.
*			Included for Linux support, where dependencies are easier to deal with.
*	
*	Usage example
*		#JFBJOY_WINDOWS
*		#JFBJOY_IMPLEMENTATION
*		#include "jfb_joystick.h"
*	
*		int main() {
*			unsigned int joystickCount;
*			Joystick* joysticks = createJoysticks(&joystickCount);
*			while(gameLoop) {
*				if (connected joysticks changed) {
*					destroyJoysticks(joysticks, joystickCount);
*					joysticks = createJoysticks(&joystickCount);
*				}
*	
*				if (joystickCount > 0) {
*					updateJoysticks(joysticks, joystickCount);
*					if (joysticks[0].buttons[0].pressed) printf("Button 0 pressed");
*					if (joysticks[0].hat & Hat_left) printf("Move left");
*				}
*			}
*			destroyJoysticks(joysticks, joystickCount);
*			joysticks = 0;
*			joystickCount = 0;
*			return 0;
*		}
*
*	Hotplugging
*		Finding what joysticks are plugged in can be very expensive; with
*		DirectInput it takes over 70ms on my PC, so only recreate the array when
*		you're sure they have changed. In Win32 you can get	notifications by
*		#including <Dbt.h> and adding the following after creating a window:
*			DEV_BROADCAST_DEVICEINTERFACE notificationFilter = { sizeof(DEV_BROADCAST_DEVICEINTERFACE), DBT_DEVTYP_DEVICEINTERFACE };
*			RegisterDeviceNotification(0, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
*		Then check for WM_DEVICECHANGE in your windows procedure. SDL offers similar
*		support with SDL_JOYDEVICEADDED/REMOVED events for the window message loop.
*/

#ifndef JFBJOY_HEADER_INCLUDED
#define JFBJOY_HEADER_INCLUDED

// Validate joystick backend settings
#if (defined(JFBJOY_DINPUT) || defined(JFBJOY_XINPUT)) && defined(JFBJOY_SDL)
	#error Joystick backend combination not supported
#endif
#if !defined(JFBJOY_DINPUT) && !defined(JFBJOY_XINPUT) && !defined(JFBJOY_SDL)
	#error No joystick backend was defined
#endif

#ifdef JFBJOY_DINPUT
	#define DIRECTINPUT_VERSION 0x0800
	#include <dinput.h>
#endif

#ifdef JFBJOY_XINPUT
	#include <wbemidl.h>
	#include <oleauto.h>
	#include <Xinput.h>
#endif

#ifdef JFBJOY_SDL
	#include "libraries/SDL/SDL.h"
	#include "libraries/SDL/SDL_joystick.h"
#endif


struct Button
{
	bool pressed; // True for one update when the button is first pressed.
	bool down;    // True while the button is held down.
};

struct Axis
{
	// Values are in the range [-1,1]
	float current;
	float previous;
};

enum Hat
{
	Hat_up=1, Hat_right=2, Hat_down=4, Hat_left=8
};

struct Joystick
{
	enum { maxButtons = 32, maxAxes = 6};

	Button buttons[maxButtons];
	Axis axes[maxAxes];
	char hat;                    // Bitflags
	char previousHat;
	char* name;                  // UTF-8

#ifdef JFBJOY_XINPUT
	unsigned int _xinputIndex;
#endif
#ifdef JFBJOY_DINPUT
	unsigned int _axisCount;
	LPDIRECTINPUTDEVICE _dinputDevice;
#endif
#ifdef JFBJOY_SDL
	SDL_Joystick* _sdlJoystick;
#endif
};

Joystick* createJoysticks(unsigned int* out_joystickCount);
void destroyJoysticks(Joystick inout_joysticks[], unsigned int joystickCount);
void updateJoysticks(Joystick inout_joysticks[], unsigned int joystickCount);

#endif // JFBJOY_HEADER_INCLUDED

#ifdef JFBJOY_IMPLEMENTATION

#ifdef JFBJOY_DINPUT
#ifdef JFBJOY_XINPUT
// Copied from https://docs.microsoft.com/en-us/windows/win32/xinput/xinput-and-directinput
//-----------------------------------------------------------------------------
// Enum each PNP device using WMI and check each device ID to see if it contains 
// "IG_" (ex. "VID_045E&PID_028E&IG_00").  If it does, then it's an XInput device
// Unfortunately this information can not be found by just using DirectInput 
//-----------------------------------------------------------------------------
BOOL isXInputDevice(const GUID* pGuidProductFromDirectInput)
{
	IWbemLocator*           pIWbemLocator = NULL;
	IEnumWbemClassObject*   pEnumDevices = NULL;
	IWbemClassObject*       pDevices[20] = { 0 };
	IWbemServices*          pIWbemServices = NULL;
	BSTR                    bstrNamespace = NULL;
	BSTR                    bstrDeviceID = NULL;
	BSTR                    bstrClassName = NULL;
	DWORD                   uReturned = 0;
	bool                    bIsXinputDevice = false;
	UINT                    iDevice = 0;
	VARIANT                 var;
	HRESULT                 hr;

	// CoInit if needed
	hr = CoInitialize(NULL);
	bool bCleanupCOM = SUCCEEDED(hr);

	// Create WMI
	hr = CoCreateInstance(__uuidof(WbemLocator),
		NULL,
		CLSCTX_INPROC_SERVER,
		__uuidof(IWbemLocator),
		(LPVOID*)&pIWbemLocator);
	if (FAILED(hr) || pIWbemLocator == NULL)
		goto LCleanup;

	bstrNamespace = SysAllocString(L"\\\\.\\root\\cimv2"); if (bstrNamespace == NULL) goto LCleanup;
	bstrClassName = SysAllocString(L"Win32_PNPEntity");   if (bstrClassName == NULL) goto LCleanup;
	bstrDeviceID = SysAllocString(L"DeviceID");          if (bstrDeviceID == NULL)  goto LCleanup;

	// Connect to WMI 
	hr = pIWbemLocator->ConnectServer(bstrNamespace, NULL, NULL, 0L,
		0L, NULL, NULL, &pIWbemServices);
	if (FAILED(hr) || pIWbemServices == NULL)
		goto LCleanup;

	// Switch security level to IMPERSONATE. 
	CoSetProxyBlanket(pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
		RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

	hr = pIWbemServices->CreateInstanceEnum(bstrClassName, 0, NULL, &pEnumDevices);
	if (FAILED(hr) || pEnumDevices == NULL)
		goto LCleanup;

	// Loop over all devices
	for (;; )
	{
		// Get 20 at a time
		hr = pEnumDevices->Next(10000, 20, pDevices, &uReturned);
		if (FAILED(hr))
			goto LCleanup;
		if (uReturned == 0)
			break;

		for (iDevice = 0; iDevice < uReturned; iDevice++)
		{
			// For each device, get its device ID
			hr = pDevices[iDevice]->Get(bstrDeviceID, 0L, &var, NULL, NULL);
			if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal != NULL)
			{
				// Check if the device ID contains "IG_".  If it does, then it's an XInput device
					// This information can not be found from DirectInput 
				if (wcsstr(var.bstrVal, L"IG_"))
				{
					// If it does, then get the VID/PID from var.bstrVal
					DWORD dwPid = 0, dwVid = 0;
					WCHAR* strVid = wcsstr(var.bstrVal, L"VID_");
					if (strVid && swscanf_s(strVid, L"VID_%4X", &dwVid) != 1)
						dwVid = 0;
					WCHAR* strPid = wcsstr(var.bstrVal, L"PID_");
					if (strPid && swscanf_s(strPid, L"PID_%4X", &dwPid) != 1)
						dwPid = 0;

					// Compare the VID/PID to the DInput device
					DWORD dwVidPid = MAKELONG(dwVid, dwPid);
					if (dwVidPid == pGuidProductFromDirectInput->Data1)
					{
						bIsXinputDevice = true;
						goto LCleanup;
					}
				}
			}
			if (pDevices[iDevice]) { pDevices[iDevice]->Release(); pDevices[iDevice] = NULL; }
		}
	}

LCleanup:
	if (bstrNamespace)
		SysFreeString(bstrNamespace);
	if (bstrDeviceID)
		SysFreeString(bstrDeviceID);
	if (bstrClassName)
		SysFreeString(bstrClassName);
	for (iDevice = 0; iDevice < 20; iDevice++)
		if (pDevices[iDevice]) { pDevices[iDevice]->Release(); pDevices[iDevice] = NULL; }
	if (pEnumDevices) { pEnumDevices->Release(); pEnumDevices = NULL; }
	if (pIWbemLocator) { pIWbemLocator->Release(); pIWbemLocator = NULL; }
	if (pIWbemServices) { pIWbemServices->Release(); pIWbemServices = NULL; }

	if (bCleanupCOM)
		CoUninitialize();

	return bIsXinputDevice;
}
#endif // JFBJOY_XINPUT

struct EnumDevicesData
{
	unsigned int joystickCount;
	Joystick* joysticks;
	LPDIRECTINPUT dinput;
};

BOOL CALLBACK DirectInputEnumDevicesCallback(LPCDIDEVICEINSTANCE instance, LPVOID pvRef)
{
#ifdef JFBJOY_XINPUT
	if (!isXInputDevice(&instance->guidProduct))
#endif
	{
		EnumDevicesData* data = (EnumDevicesData*)pvRef;
		data->joystickCount += 1;
		data->joysticks = (Joystick*)realloc(data->joysticks, data->joystickCount * sizeof(Joystick));

		Joystick joystick = { 0 };
		data->dinput->CreateDevice(instance->guidInstance, &joystick._dinputDevice, NULL);
		DIDEVCAPS caps = { sizeof(DIDEVCAPS) };
		joystick._dinputDevice->GetCapabilities(&caps);
		joystick._axisCount = caps.dwAxes;
		
		// Copy display-name
		DIDEVICEINSTANCE deviceInfo = { sizeof(DIDEVICEINSTANCE) };
		joystick._dinputDevice->GetDeviceInfo(&deviceInfo);
#ifdef UNICODE
		int length = WideCharToMultiByte(CP_UTF8, 0, deviceInfo.tszProductName, -1, 0, 0, 0, 0);
		joystick.name = (char*)malloc(length);
		WideCharToMultiByte(CP_UTF8, 0, deviceInfo.tszProductName, -1, joystick.name, length, 0, 0);
#else
		// Convert multibyte to UTF-16, then to UTF-8
		// (Is there a way to go directly to UTF-8?)
		int length = MultiByteToWideChar(GetACP(), 0, deviceInfo.tszProductName, -1, 0, 0);
		wchar_t* utf16Buffer = (wchar_t*)malloc(length*sizeof(wchar_t));
		MultiByteToWideChar(GetACP(), 0, deviceInfo.tszProductName, -1, utf16Buffer, length);
		length = WideCharToMultiByte(CP_UTF8, 0, utf16Buffer, -1, 0, 0, 0, 0);
		joystick.name = (char*)malloc(length*sizeof(char));
		WideCharToMultiByte(CP_UTF8, 0, utf16Buffer, -1, joystick.name, length, 0, 0);
		free(utf16Buffer);
#endif

		joystick._dinputDevice->SetCooperativeLevel(GetActiveWindow(), DISCL_NONEXCLUSIVE);
		joystick._dinputDevice->SetDataFormat(&c_dfDIJoystick);
		joystick._dinputDevice->Acquire();


		data->joysticks[data->joystickCount - 1] = joystick;
	}

	return DIENUM_CONTINUE;
}
#endif // JFBJOY_DINPUT

Joystick* createJoysticks(unsigned int* out_joystickCount)
{
	Joystick* joysticks = 0;
	unsigned int joystickCount = 0;

#ifdef JFBJOY_XINPUT
	for (unsigned int i = 0; i < 4; ++i)
	{
		XINPUT_STATE state;
		DWORD errorCode = XInputGetState(i, &state);
		if (errorCode == ERROR_SUCCESS)
		{
			Joystick joy = { 0 };
			joy._xinputIndex = i;
			
			// There's no way to associate an XInput controller with a DirectInput one.
			// Since we can't use DirectInput to get the controller's real name, we'll make one up.
			char name[] = "XBox Controller #";
			name[16] = '1' + i;
			joy.name = (char*)malloc(sizeof(name));
			strcpy_s(joy.name, sizeof(name), name);

			joystickCount += 1;
			joysticks = (Joystick*)realloc(joysticks, joystickCount * sizeof(Joystick));
			joysticks[joystickCount - 1] = joy;
		}
	}	
#endif

#ifdef JFBJOY_DINPUT
	LPDIRECTINPUTDEVICE8 device = { 0 };
	HINSTANCE hInstance = GetModuleHandle(0);
	LPDIRECTINPUT directInput;
	HRESULT result = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, 0);
	EnumDevicesData data = { 0 };
	data.dinput = directInput;
	data.joystickCount = joystickCount;
	data.joysticks = joysticks;
	result = directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, DirectInputEnumDevicesCallback, (void*)&data, DIEDFL_ALLDEVICES);
	directInput->Release();
	
	joystickCount = data.joystickCount;
	joysticks = data.joysticks;
#endif

#ifdef JFBJOY_SDL
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	joystickCount = SDL_NumJoysticks();
	joysticks = (Joystick*) calloc(joystickCount, sizeof(Joystick));
	for (unsigned int i = 0; i < joystickCount; ++i) {
		joysticks[i]._sdlJoystick = SDL_JoystickOpen(i);
	}
#endif

	*out_joystickCount = joystickCount;
	return joysticks;
}

void destroyJoysticks(Joystick inout_joysticks[], unsigned int joystickCount)
{
	for (unsigned int i = 0; i < joystickCount; ++i)
	{
		free(inout_joysticks[i].name);
#ifdef JFBJOY_DINPUT
		if (inout_joysticks[i]._dinputDevice) {
			inout_joysticks[i]._dinputDevice->Unacquire();
			inout_joysticks[i]._dinputDevice->Release();
		}
#endif
#ifdef JFBJOY_SDL
		SDL_JoystickClose(inout_joysticks[i]._sdlJoystick);
#endif
	}

	free(inout_joysticks);
}

void updateButton(Button* inout_button, unsigned int isDown)
{
	if (isDown) {
		inout_button->pressed = !inout_button->down;
		inout_button->down = true;
	}
	else {
		inout_button->pressed = false;
		inout_button->down = false;
	}
}

void updateJoysticks(Joystick inout[], unsigned int joystickCount)
{
#ifdef JFBJOY_DINPUT 
	for (unsigned int joystickIndex = 0; joystickIndex < joystickCount; ++joystickIndex)
	{
		Joystick* joystick = &inout[joystickIndex];
		if (joystick->_dinputDevice)
		{
			DWORD stateSize = 0;
			DIJOYSTATE state = { 0 };
			if (joystick->_dinputDevice->GetDeviceState(sizeof(state), &state) == DI_OK)
			{
				// Buttons
				for (unsigned int buttonIndex = 0; buttonIndex < Joystick::maxButtons; ++buttonIndex) {
					updateButton(&joystick->buttons[buttonIndex], state.rgbButtons[buttonIndex]);
				}

				// Axes
				LONG axes[] = {
					state.lX,
					state.lY,
					state.lZ,
					state.lRx,
					state.lRy,
					state.lRz
				};
				unsigned int axisCount = sizeof(axes) / sizeof(axes[0]);
				if (axisCount > Joystick::maxAxes) axisCount = Joystick::maxAxes;
				for (unsigned int axisIndex = 0; axisIndex < axisCount; ++axisIndex) {
					joystick->axes[axisIndex].previous = joystick->axes[axisIndex].current;
					joystick->axes[axisIndex].current = (float)(axes[axisIndex]  - SHRT_MAX) / (float)SHRT_MAX;
				}

				// Hat
				joystick->previousHat = joystick->hat;
				joystick->hat = 0;
				DWORD hat = state.rgdwPOV[0];
				if (hat != -1) {
					if (hat > 27000 || hat < 9000) joystick->hat |= Hat_up;
					if (hat > 0 && hat < 18000)    joystick->hat |= Hat_right;
					if (hat > 9000 && hat < 27000) joystick->hat |= Hat_down;
					if (hat > 18000)               joystick->hat |= Hat_left;
				}
			}
		}
	}
#endif // JFBJOY_DINPUT

#ifdef JFBJOY_XINPUT
	for (unsigned int joystickIndex = 0; joystickIndex < joystickCount; ++joystickIndex)
	{
		Joystick* joystick = &inout[joystickIndex];
#ifdef JFBJOY_DINPUT
		if (!joystick->_dinputDevice)
#endif
		{
			XINPUT_STATE state;
			if (XInputGetState(joystick->_xinputIndex, &state) == ERROR_SUCCESS)
			{
				updateButton(&joystick->buttons[0],  state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
				updateButton(&joystick->buttons[1],  state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
				updateButton(&joystick->buttons[2],  state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
				updateButton(&joystick->buttons[3],  state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
				updateButton(&joystick->buttons[4],  state.Gamepad.wButtons & XINPUT_GAMEPAD_START);
				updateButton(&joystick->buttons[5],  state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK);
				updateButton(&joystick->buttons[6],  state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
				updateButton(&joystick->buttons[7],  state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
				updateButton(&joystick->buttons[8],  state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
				updateButton(&joystick->buttons[9],  state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
				updateButton(&joystick->buttons[10], state.Gamepad.wButtons & XINPUT_GAMEPAD_A);
				updateButton(&joystick->buttons[11], state.Gamepad.wButtons & XINPUT_GAMEPAD_B);
				updateButton(&joystick->buttons[12], state.Gamepad.wButtons & XINPUT_GAMEPAD_X);
				updateButton(&joystick->buttons[13], state.Gamepad.wButtons & XINPUT_GAMEPAD_Y);

				for (unsigned int axisIndex = 0; axisIndex < 6; ++axisIndex) {
					joystick->axes[axisIndex].previous = joystick->axes[axisIndex].current;
				}
				joystick->axes[0].current = state.Gamepad.sThumbLX   / (float)SHRT_MAX;
				joystick->axes[1].current = -state.Gamepad.sThumbLY  / (float)SHRT_MAX;
				joystick->axes[2].current = state.Gamepad.sThumbRX   / (float)SHRT_MAX;
				joystick->axes[3].current = -state.Gamepad.sThumbRX  / (float)SHRT_MAX;
				joystick->axes[4].current = state.Gamepad.bLeftTrigger  / (float)UCHAR_MAX;
				joystick->axes[5].current = state.Gamepad.bRightTrigger / (float)UCHAR_MAX;
			}
		}
	}
#endif // JFBJOY_XINPUT

#ifdef JFBJOY_SDL
	SDL_JoystickUpdate();
	for (unsigned int joystickIndex=0; joystickIndex < joystickCount; ++joystickIndex)
	{
		Joystick* joystick = &inout[joystickIndex];
		// Buttons
		for (int buttonIndex=0; buttonIndex < Joystick::maxButtons; ++buttonIndex) {
			updateButton(&joystick->buttons[buttonIndex], SDL_JoystickGetButton(joystick->_sdlJoystick, buttonIndex));
		}
		// Hat
		joystick->previousHat = joystick->hat;
		joystick->hat = SDL_JoystickGetHat(joystick->_sdlJoystick, 0);
		// Axis
		for (int axisIndex=0; axisIndex < Joystick::maxAxes; ++axisIndex) {
			joystick->axes[axisIndex].previous = joystick->axes[axisIndex].current;
			joystick->axes[axisIndex].current = SDL_JoystickGetAxis(joystick->_sdlJoystick, axisIndex) / (float)SHRT_MAX;
		}
	}
#endif // JFBJOY_SDL
}

#endif // JFBJOY_IMPLEMENTATION


/*
MIT License

Copyright (c) 2020 Jacob Bell

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/