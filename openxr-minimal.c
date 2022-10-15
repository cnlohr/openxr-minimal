// Minimal C OpenXR implementation (based on https://github.com/hyperlogic/openxrstub/blob/main/src/main.cpp)
// Adapted for use on all major platforms.
// No reliance on any libraries.
// 
// That portion is: 
//	Copyright (c) 2020 Anthony J. Thibault
// The rest is:
//	Copyright (c) 2022 Charles Lohr
//
// Under the MIT/x11 License.
//

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "os_generic.h"

#define XR_USE_GRAPHICS_API_OPENGL
#if defined(USE_WINDOWS)
#define XR_USE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#else
#define XR_USE_PLATFORM_XLIB
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define CNFGOGL
#define CNFGOGL_NEED_EXTENSION
#define CNFG_IMPLEMENTATION
#include "rawdraw_sf.h"

// For limited OpenGL Platforms, Like Windows.
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION   0x821B
#define GL_MINOR_VERSION   0x821C
#define GL_FRAMEBUFFER	 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT  0x8D00
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

void (*minXRglGenFramebuffers)( GLsizei n, GLuint *ids );
void (*minXRglBindFramebuffer)( GLenum target, GLuint framebuffer );
void (*minXRglFramebufferTexture2D)( GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level );
 
void EnumOpenGLExtensions()
{
	minXRglGenFramebuffers = CNFGGetProcAddress( "glGenFramebuffers" );
	minXRglBindFramebuffer = CNFGGetProcAddress( "glBindFramebuffer" );
	minXRglFramebufferTexture2D = CNFGGetProcAddress( "glFramebufferTexture2D" );
}





XrExtensionProperties * extensionProps;
int numExtensions;

//XrApiLayerProperties * layerProps;
//int numLayerProps;

XrViewConfigurationView * viewConfigs;
int numViewConfigs;

// numColorDepthPairs * 2 
GLuint * colorDepthPairs;
int numColorDepthPairs;

XrInstance instance = XR_NULL_HANDLE;
XrSystemId systemId = XR_NULL_HANDLE;
XrSession session = XR_NULL_HANDLE;
XrActionSet actionSet = XR_NULL_HANDLE;
XrSpace stageSpace = XR_NULL_HANDLE;
GLuint frameBuffer;

struct SwapchainInfo
{
	XrSwapchain handle;
	int32_t width;
	int32_t height;
};
struct SwapchainInfo * swapchains;
XrSwapchainImageOpenGLKHR ** swapchainImages;
uint32_t * swapchainLengths;

struct RenderInfo
{
	GLuint program;
} renderInfo;

// For debugging.
int printAll = 1;

static int CheckResult( XrInstance instance, XrResult result, const char* str )
{
	if( XR_SUCCEEDED( result ))
	{
		return 1;
	}

	if( instance != XR_NULL_HANDLE)
	{
		char resultString[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(instance, result, resultString);
		printf( "%s [%s]\n", str, resultString );
	}
	else
	{
		printf( "%s\n", str );
	}
	return 0;
}

int EnumerateExtensions( XrExtensionProperties ** extensionProps )
{
	XrResult result;
	uint32_t extensionCount = 0;
	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &extensionCount, NULL);
	if( !CheckResult(NULL, result, "xrEnumerateInstanceExtensionProperties failed"))
		return 0;
	
	*extensionProps = realloc( *extensionProps, extensionCount * sizeof( XrExtensionProperties ) );
	for( uint32_t i = 0; i < extensionCount; i++ )
	{
		(*extensionProps)[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		(*extensionProps)[i].next = NULL;
	}

	result = xrEnumerateInstanceExtensionProperties( NULL, extensionCount, &extensionCount, *extensionProps );
	if( !CheckResult( NULL, result, "xrEnumerateInstanceExtensionProperties failed" ) )
		return 0;

#if 1
	if( printAll )
	{
		printf("%d extensions:\n", extensionCount);
		for( uint32_t i = 0; i < extensionCount; ++i )
		{
			printf( "	%s\n", (*extensionProps)[i].extensionName );
		}
	}
#endif
	return extensionCount;
}

int ExtensionSupported( XrExtensionProperties * extensions, int extensionsCount, const char* extensionName)
{
	int supported = 0;
	int i;
	for( i = 0; i < extensionsCount; i++ )
	{
		if( !strcmp(extensionName, extensions[i].extensionName))
		{
			supported = 1;
		}
	}
	return supported;
}

#if 0
int EnumerateLayers( XrApiLayerProperties ** layerProps )
{
	uint32_t layerCount;
	XrResult result = xrEnumerateApiLayerProperties(0, &layerCount, NULL);
	if( !CheckResult(NULL, result, "xrEnumerateApiLayerProperties"))
	{
		return 0;
	}

	*layerProps = realloc( *layerProps, layerCount * sizeof(XrApiLayerProperties) );
	for( uint32_t i = 0; i < layerCount; i++) {
		(*layerProps)[i].type = XR_TYPE_API_LAYER_PROPERTIES;
		(*layerProps)[i].next = NULL;
	}
	result = xrEnumerateApiLayerProperties( layerCount, &layerCount, *layerProps );
	if( !CheckResult(NULL, result, "xrEnumerateApiLayerProperties"))
	{
		return 0;
	}

#if 1
	if( printAll )
	{
		printf("%d layers:\n", layerCount);
		for( uint32_t i = 0; i < layerCount; i++)
		{
			printf("	%s, %s\n", (*layerProps)[i].layerName, (*layerProps)[i].description);
		}
	}
#endif
	return layerCount;
}
#endif

int CreateInstance(XrInstance * instance)
{
	// create openxr instance
	XrResult result;
	const char* const enabledExtensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
	XrInstanceCreateInfo ici;
	ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
	ici.next = NULL;
	ici.createFlags = 0;
	ici.enabledExtensionCount = 1;
	ici.enabledExtensionNames = enabledExtensions;
	ici.enabledApiLayerCount = 0;
	ici.enabledApiLayerNames = NULL;
	strcpy(ici.applicationInfo.applicationName, "openxr-minimal");
	ici.applicationInfo.engineName[0] = '\0';
	ici.applicationInfo.applicationVersion = 1;
	ici.applicationInfo.engineVersion = 0;
	ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

	result = xrCreateInstance(&ici, instance);
	if (!CheckResult(NULL, result, "xrCreateInstance failed"))
	{
		return 0;
	}

#if 1
	if ( printAll)
	{
		XrInstanceProperties ip;
		ip.type = XR_TYPE_INSTANCE_PROPERTIES;
		ip.next = NULL;

		result = xrGetInstanceProperties( *instance, &ip );
		if( !CheckResult( *instance, result, "xrGetInstanceProperties failed" ) )
		{
			return 0;
		}

		printf("Runtime Name: %s\n", ip.runtimeName);
		printf("Runtime Version: %d.%d.%d\n",
			   XR_VERSION_MAJOR(ip.runtimeVersion),
			   XR_VERSION_MINOR(ip.runtimeVersion),
			   XR_VERSION_PATCH(ip.runtimeVersion));
	}
#endif
	return 1;
}


int GetSystemId(XrInstance instance, XrSystemId * systemId)
{
	XrResult result;
	XrSystemGetInfo sgi;
	sgi.type = XR_TYPE_SYSTEM_GET_INFO;
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	sgi.next = NULL;

	result = xrGetSystem(instance, &sgi, systemId);
	if (!CheckResult(instance, result, "xrGetSystemFailed"))
	{
		return 0;
	}

#if 1
	if ( printAll)
	{
		XrSystemProperties sp = { 0 };
		sp.type = XR_TYPE_SYSTEM_PROPERTIES;

		result = xrGetSystemProperties(instance, *systemId, &sp);
		if (!CheckResult(instance, result, "xrGetSystemProperties failed"))
		{
			return 0;
		}

		printf("System properties for system \"%s\":\n", sp.systemName);
		printf("	maxLayerCount: %d\n", sp.graphicsProperties.maxLayerCount);
		printf("	maxSwapChainImageHeight: %d\n", sp.graphicsProperties.maxSwapchainImageHeight);
		printf("	maxSwapChainImageWidth: %d\n", sp.graphicsProperties.maxSwapchainImageWidth);
		printf("	Orientation Tracking: %s\n", sp.trackingProperties.orientationTracking ? "true" : "false");
		printf("	Position Tracking: %s\n", sp.trackingProperties.positionTracking ? "true" : "false");
	}
#endif

	return 1;
}

int EnumerateViewConfigs(XrInstance instance, XrSystemId systemId, XrViewConfigurationView ** viewConfigs)
{
	XrResult result;
	uint32_t viewCount;
	XrViewConfigurationType stereoViewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	result = xrEnumerateViewConfigurationViews(instance, systemId, stereoViewConfigType, 0, &viewCount, NULL);
	if (!CheckResult(instance, result, "xrEnumerateViewConfigurationViews"))
	{
		return 0;
	}

	*viewConfigs = realloc( *viewConfigs, viewCount * sizeof(XrViewConfigurationView) );
	for (uint32_t i = 0; i < viewCount; i++)
	{
		(*viewConfigs)[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		(*viewConfigs)[i].next = NULL;
	}

	result = xrEnumerateViewConfigurationViews(instance, systemId, stereoViewConfigType, viewCount, &viewCount, *viewConfigs);
	if (!CheckResult(instance, result, "xrEnumerateViewConfigurationViews"))
	{
		return 0;
	}

#if 1
	if (printAll)
	{
		printf("%d viewConfigs:\n", viewCount);
		for (uint32_t i = 0; i < viewCount; i++)
		{
			printf("	viewConfigs[%d]:\n", i);
			printf("		recommendedImageRectWidth: %d\n", (*viewConfigs)[i].recommendedImageRectWidth);
			printf("		maxImageRectWidth: %d\n", (*viewConfigs)[i].maxImageRectWidth);
			printf("		recommendedImageRectHeight: %d\n", (*viewConfigs)[i].recommendedImageRectHeight);
			printf("		maxImageRectHeight: %d\n", (*viewConfigs)[i].maxImageRectHeight);
			printf("		recommendedSwapchainSampleCount: %d\n", (*viewConfigs)[i].recommendedSwapchainSampleCount);
			printf("		maxSwapchainSampleCount: %d\n", (*viewConfigs)[i].maxSwapchainSampleCount);
		}
	}
#endif
	return viewCount;
}

int CreateSession(XrInstance instance, XrSystemId systemId, XrSession * session)
{
	XrResult result;

	// check if opengl version is sufficient.
	{
		XrGraphicsRequirementsOpenGLKHR reqs;
		reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
		reqs.next = NULL;
		PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = NULL;
		result = xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&pfnGetOpenGLGraphicsRequirementsKHR);
		if (!CheckResult(instance, result, "xrGetInstanceProcAddr"))
		{
			return 0;
		}

		result = pfnGetOpenGLGraphicsRequirementsKHR(instance, systemId, &reqs);
		if (!CheckResult(instance, result, "GetOpenGLGraphicsRequirementsPKR"))
		{
			return 0;
		}

		GLint major = 0;
		GLint minor = 0;
		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);
		const XrVersion desiredApiVersion = XR_MAKE_VERSION(major, minor, 0);

#if 1
		if (printAll)
		{
			printf("current OpenGL version: %d.%d.%d\n", XR_VERSION_MAJOR(desiredApiVersion),
				   XR_VERSION_MINOR(desiredApiVersion), XR_VERSION_PATCH(desiredApiVersion));
			printf("minimum OpenGL version: %d.%d.%d\n", XR_VERSION_MAJOR(reqs.minApiVersionSupported),
				   XR_VERSION_MINOR(reqs.minApiVersionSupported), XR_VERSION_PATCH(reqs.minApiVersionSupported));
		}
#endif
		if (reqs.minApiVersionSupported > desiredApiVersion)
		{
			printf("Runtime does not support desired Graphics API and/or version\n");
			return 0;
		}
	}

	XrGraphicsBindingOpenGLWin32KHR glBinding;
	glBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	glBinding.next = NULL;
	glBinding.hDC = wglGetCurrentDC();
	glBinding.hGLRC = wglGetCurrentContext();

	XrSessionCreateInfo sci;
	sci.type = XR_TYPE_SESSION_CREATE_INFO;
	sci.next = &glBinding;
	sci.systemId = systemId;

	result = xrCreateSession(instance, &sci, session);
	if (!CheckResult(instance, result, "xrCreateSession"))
	{
		return 0;
	}
	
	return 1;
}


int CreateActions(XrInstance instance, XrSystemId systemId, XrSession session, XrActionSet * actionSet)
{
	XrResult result;

	// create action set
	XrActionSetCreateInfo asci;
	asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	asci.next = NULL;
	strcpy(asci.actionSetName, "gameplay");
	strcpy(asci.localizedActionSetName, "Gameplay");
	asci.priority = 0;
	result = xrCreateActionSet(instance, &asci, actionSet);
	if (!CheckResult(instance, result, "xrCreateActionSet"))
	{
		return 0;
	}

	XrPath handPath[2] = { XR_NULL_PATH, XR_NULL_PATH };
	xrStringToPath(instance, "/user/hand/left", &handPath[0]);
	xrStringToPath(instance, "/user/hand/right", &handPath[1]);
	if (!CheckResult(instance, result, "xrStringToPath"))
	{
		return 0;
	}

	XrAction grabAction = XR_NULL_HANDLE;
	XrActionCreateInfo aci;
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.next = NULL;
	aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(aci.actionName, "grab_object");
	strcpy(aci.localizedActionName, "Grab Object");
	aci.countSubactionPaths = 2;
	aci.subactionPaths = handPath;
	result = xrCreateAction(*actionSet, &aci, &grabAction);
	if (!CheckResult(instance, result, "xrCreateAction"))
	{
		return 0;
	}

	XrAction poseAction = XR_NULL_HANDLE;
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.next = NULL;
	aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy(aci.actionName, "hand_pose");
	strcpy(aci.localizedActionName, "Hand Pose");
	aci.countSubactionPaths = 2;
	aci.subactionPaths = handPath;
	result = xrCreateAction(*actionSet, &aci, &poseAction);
	if (!CheckResult(instance, result, "xrCreateAction"))
	{
		return 0;
	}

	XrAction vibrateAction = XR_NULL_HANDLE;
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.next = NULL;
	aci.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
	strcpy(aci.actionName, "vibrate_hand");
	strcpy(aci.localizedActionName, "Vibrate Hand");
	aci.countSubactionPaths = 2;
	aci.subactionPaths = handPath;
	result = xrCreateAction(*actionSet, &aci, &vibrateAction);
	if (!CheckResult(instance, result, "xrCreateAction"))
	{
		return 0;
	}

	XrAction quitAction = XR_NULL_HANDLE;
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.next = NULL;
	aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(aci.actionName, "quit_session");
	strcpy(aci.localizedActionName, "Quit Session");
	aci.countSubactionPaths = 2;
	aci.subactionPaths = handPath;
	result = xrCreateAction(*actionSet, &aci, &quitAction);
	if (!CheckResult(instance, result, "xrCreateAction"))
	{
		return 0;
	}

	XrPath selectPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrPath squeezeValuePath[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrPath squeezeClickPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrPath posePath[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrPath hapticPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrPath menuClickPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
	xrStringToPath(instance, "/user/hand/left/input/select/click", &selectPath[0]);
	xrStringToPath(instance, "/user/hand/right/input/select/click", &selectPath[1]);
	xrStringToPath(instance, "/user/hand/left/input/squeeze/value", &squeezeValuePath[0]);
	xrStringToPath(instance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[1]);
	xrStringToPath(instance, "/user/hand/left/input/squeeze/click", &squeezeClickPath[0]);
	xrStringToPath(instance, "/user/hand/right/input/squeeze/click", &squeezeClickPath[1]);
	xrStringToPath(instance, "/user/hand/left/input/grip/pose", &posePath[0]);
	xrStringToPath(instance, "/user/hand/right/input/grip/pose", &posePath[1]);
	xrStringToPath(instance, "/user/hand/left/output/haptic", &hapticPath[0]);
	xrStringToPath(instance, "/user/hand/right/output/haptic", &hapticPath[1]);
	xrStringToPath(instance, "/user/hand/left/input/menu/click", &menuClickPath[0]);
	xrStringToPath(instance, "/user/hand/right/input/menu/click", &menuClickPath[1]);
	if (!CheckResult(instance, result, "xrStringToPath"))
	{
		return 0;
	}

	// KHR Simple
	{
		XrPath interactionProfilePath = XR_NULL_PATH;
		xrStringToPath(instance, "/interaction_profiles/khr/simple_controller", &interactionProfilePath);
		XrActionSuggestedBinding bindings[8] = {
			{grabAction, selectPath[0]},
			{grabAction, selectPath[1]},
			{poseAction, posePath[0]},
			{poseAction, posePath[1]},
			{quitAction, menuClickPath[0]},
			{quitAction, menuClickPath[1]},
			{vibrateAction, hapticPath[0]},
			{vibrateAction, hapticPath[1]}
		};

		XrInteractionProfileSuggestedBinding suggestedBindings;
		suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
		suggestedBindings.next = NULL;
		suggestedBindings.interactionProfile = interactionProfilePath;
		suggestedBindings.suggestedBindings = bindings;
		suggestedBindings.countSuggestedBindings = sizeof( bindings ) / sizeof( bindings[0] );
		result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
		if (!CheckResult(instance, result, "xrSuggestInteractionProfileBindings"))
		{
			return 0;
		}
	}
#if 0
	// oculus touch
	{
		XrPath interactionProfilePath = XR_NULL_PATH;
		xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &interactionProfilePath);
		XrActionSuggestedBinding bindings[7] = {
			{grabAction, squeezeValuePath[0]},
			{grabAction, squeezeValuePath[1]},
			{poseAction, posePath[0]},
			{poseAction, posePath[1]},
			{quitAction, menuClickPath[0]},
			//{quitAction, menuClickPath[1]},  // no menu button on right controller?
			{vibrateAction, hapticPath[0]},
			{vibrateAction, hapticPath[1]}
		};

		XrInteractionProfileSuggestedBinding suggestedBindings;
		suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
		suggestedBindings.next = NULL;
		suggestedBindings.interactionProfile = interactionProfilePath;
		suggestedBindings.suggestedBindings = bindings;
		suggestedBindings.countSuggestedBindings = sizeof( bindings ) / sizeof( bindings[0] );
		result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
		if (!CheckResult(instance, result, "xrSuggestInteractionProfileBindings (oculus)"))
		{
			return 0;
		}
	}

	// vive
	{
		XrPath interactionProfilePath = XR_NULL_PATH;
		xrStringToPath(instance, "/interaction_profiles/htc/vive_controller", &interactionProfilePath);
		std::vector<XrActionSuggestedBinding> bindings = {
			{grabAction, squeezeClickPath[0]},
			{grabAction, squeezeClickPath[1]},
			{poseAction, posePath[0]},
			{poseAction, posePath[1]},
			{quitAction, menuClickPath[0]},
			{quitAction, menuClickPath[1]},
			{vibrateAction, hapticPath[0]},
			{vibrateAction, hapticPath[1]}
		};

		XrInteractionProfileSuggestedBinding suggestedBindings;
		suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
		suggestedBindings.next = NULL;
		suggestedBindings.interactionProfile = interactionProfilePath;
		suggestedBindings.suggestedBindings = bindings.data();
		suggestedBindings.countSuggestedBindings = sizeof( bindings ) / sizeof( bindings[0] );
		result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
		if (!CheckResult(instance, result, "xrSuggestInteractionProfileBindings (vive)"))
		{
			return 0;
		}
	}

	// microsoft mixed reality
	{
		XrPath interactionProfilePath = XR_NULL_PATH;
		xrStringToPath(instance, "/interaction_profiles/microsoft/motion_controller", &interactionProfilePath);
		std::vector<XrActionSuggestedBinding> bindings = {
			{grabAction, squeezeClickPath[0]},
			{grabAction, squeezeClickPath[1]},
			{poseAction, posePath[0]},
			{poseAction, posePath[1]},
			{quitAction, menuClickPath[0]},
			{quitAction, menuClickPath[1]},
			{vibrateAction, hapticPath[0]},
			{vibrateAction, hapticPath[1]}
		};

		XrInteractionProfileSuggestedBinding suggestedBindings;
		suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
		suggestedBindings.next = NULL;
		suggestedBindings.interactionProfile = interactionProfilePath;
		suggestedBindings.suggestedBindings = bindings.data();
		suggestedBindings.countSuggestedBindings = sizeof( bindings ) / sizeof( bindings[0] );
		result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
		if (!CheckResult(instance, result, "xrSuggestInteractionProfileBindings"))
		{
			return false;
		}
	}
#endif

	XrSpace handSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
	XrActionSpaceCreateInfo aspci = { 0 };
	aspci.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
	aspci.next = NULL;
	aspci.action = poseAction;
	XrPosef identity = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
	aspci.poseInActionSpace = identity;

	aspci.subactionPath = handPath[0];
	result = xrCreateActionSpace(session, &aspci, &handSpace[0]);
	if (!CheckResult(instance, result, "xrCreateActionSpace"))
	{
		return 0;
	}

	aspci.subactionPath = handPath[1];
	result = xrCreateActionSpace(session, &aspci, &handSpace[1]);
	if (!CheckResult(instance, result, "xrCreateActionSpace"))
	{
		return 0;
	}

	XrSessionActionSetsAttachInfo sasai;
	sasai.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	sasai.next = NULL;
	sasai.countActionSets = 1;
	sasai.actionSets = actionSet;
	result = xrAttachSessionActionSets(session, &sasai);
	if (!CheckResult(instance, result, "xrSessionActionSetsAttachInfo"))
	{
		return 0;
	}

	return 1;
}


int CreateStageSpace(XrInstance instance, XrSystemId systemId, XrSession session, XrSpace * stageSpace)
{
	XrResult result;
#if 1
	if (printAll)
	{
		uint32_t referenceSpacesCount;
		result = xrEnumerateReferenceSpaces(session, 0, &referenceSpacesCount, NULL);
		if (!CheckResult(instance, result, "xrEnumerateReferenceSpaces"))
		{
			return 0;
		}

		XrReferenceSpaceType * referenceSpaces = malloc( referenceSpacesCount * sizeof(XrReferenceSpaceType) );
		int i;
		for( i = 0; i < referenceSpacesCount; i++ )
			referenceSpaces[i] = XR_REFERENCE_SPACE_TYPE_VIEW;
		result = xrEnumerateReferenceSpaces(session, referenceSpacesCount, &referenceSpacesCount, referenceSpaces );
		if (!CheckResult(instance, result, "xrEnumerateReferenceSpaces"))
		{
			return 0;
		}

		printf("referenceSpaces:\n");
		for (uint32_t i = 0; i < referenceSpacesCount; i++)
		{
			switch (referenceSpaces[i])
			{
			case XR_REFERENCE_SPACE_TYPE_VIEW:
				printf("	XR_REFERENCE_SPACE_TYPE_VIEW\n");
				break;
			case XR_REFERENCE_SPACE_TYPE_LOCAL:
				printf("	XR_REFERENCE_SPACE_TYPE_LOCAL\n");
				break;
			case XR_REFERENCE_SPACE_TYPE_STAGE:
				printf("	XR_REFERENCE_SPACE_TYPE_STAGE\n");
				break;
			default:
				printf("	XR_REFERENCE_SPACE_TYPE_%d\n", referenceSpaces[i]);
				break;
			}
		}
	}
#endif
	XrPosef identityPose = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };

	XrReferenceSpaceCreateInfo rsci;
	rsci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	rsci.next = NULL;
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	rsci.poseInReferenceSpace = identityPose;
	result = xrCreateReferenceSpace(session, &rsci, stageSpace);
	if (!CheckResult(instance, result, "xrCreateReferenceSpace"))
	{
		return 0;
	}

	return 1;
}


int CreateSwapchains(XrInstance instance, XrSession session,
					  XrViewConfigurationView * viewConfigs, int viewConfigsCount,
					  struct SwapchainInfo ** swapchains, // Will allocate to viewConfigsCount
					  XrSwapchainImageOpenGLKHR *** swapchainImages,
					  uint32_t ** swapchainLengths) // Actually array of pointers to pointers
{
	XrResult result;
	uint32_t swapchainFormatCount;
	result = xrEnumerateSwapchainFormats(session, 0, &swapchainFormatCount, NULL);
	if (!CheckResult(instance, result, "xrEnumerateSwapchainFormats"))
	{
		return 0;
	}

	int64_t swapchainFormats[swapchainFormatCount];
	result = xrEnumerateSwapchainFormats(session, swapchainFormatCount, &swapchainFormatCount, swapchainFormats);
	if (!CheckResult(instance, result, "xrEnumerateSwapchainFormats"))
	{
		return 0;
	}

	// TODO: pick a format. XXX TODO
	int64_t swapchainFormatToUse = swapchainFormats[0];

	*swapchains = realloc( *swapchains, viewConfigsCount * sizeof( struct SwapchainInfo ) );
	*swapchainLengths = realloc( *swapchainLengths, viewConfigsCount * sizeof( uint32_t ) );
	for (uint32_t i = 0; i < viewConfigsCount; i++)
	{
		XrSwapchainCreateInfo sci;
		sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
		sci.next = NULL;
		sci.createFlags = 0;
		sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		sci.format = swapchainFormatToUse;
		sci.sampleCount = 1;
		sci.width = viewConfigs[i].recommendedImageRectWidth;
		sci.height = viewConfigs[i].recommendedImageRectHeight;
		sci.faceCount = 1;
		sci.arraySize = 1;
		sci.mipCount = 1;

		XrSwapchain swapchainHandle;
		result = xrCreateSwapchain(session, &sci, &swapchainHandle);
		if (!CheckResult(instance, result, "xrCreateSwapchain"))
		{
			return 0;
		}

		(*swapchains)[i].handle = swapchainHandle;
		(*swapchains)[i].width = sci.width;
		(*swapchains)[i].height = sci.height;

		result = xrEnumerateSwapchainImages((*swapchains)[i].handle, 0, &(*swapchainLengths)[i], NULL);
		if (!CheckResult(instance, result, "xrEnumerateSwapchainImages"))
		{
			return 0;
		}
	}

	*swapchainImages = realloc( *swapchainImages, viewConfigsCount * sizeof( XrSwapchainImageOpenGLKHR * ) ); 
	for (uint32_t i = 0; i < viewConfigsCount; i++)
	{
		(*swapchainImages)[i] = malloc( (*swapchainLengths)[i] * sizeof(XrSwapchainImageOpenGLKHR) );
		for (uint32_t j = 0; j < (*swapchainLengths)[i]; j++)
		{
			(*swapchainImages)[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			(*swapchainImages)[i][j].next = NULL;
		}

		result = xrEnumerateSwapchainImages((*swapchains)[i].handle, (*swapchainLengths)[i], &(*swapchainLengths)[i],
											(XrSwapchainImageBaseHeader*)((*swapchainImages)[i]));

		if (!CheckResult(instance, result, "xrEnumerateSwapchainImages"))
		{
			return 0;
		}
	}
	
	return 1;
}


int BeginSession(XrInstance instance, XrSystemId systemId, XrSession session)
{
	XrResult result;
	XrViewConfigurationType stereoViewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XrSessionBeginInfo sbi;
	sbi.type = XR_TYPE_SESSION_BEGIN_INFO;
	sbi.next = NULL;
	sbi.primaryViewConfigurationType = stereoViewConfigType;

	result = xrBeginSession(session, &sbi);
	if (!CheckResult(instance, result, "xrBeginSession"))
	{
		return 0;
	}

	return 1;
}


int SyncInput(XrInstance instance, XrSession session, XrActionSet actionSet)
{
	XrResult result;

	// syncInput
	XrActiveActionSet aas;
	aas.actionSet = actionSet;
	aas.subactionPath = XR_NULL_PATH;
	XrActionsSyncInfo asi;
	asi.type = XR_TYPE_ACTIONS_SYNC_INFO;
	asi.next = NULL;
	asi.countActiveActionSets = 1;
	asi.activeActionSets = &aas;
	result = xrSyncActions(session, &asi);
	if (!CheckResult(instance, result, "xrSyncActions"))
	{
		return 0;
	}

	// Todo's from original author.
	// AJT: TODO: xrGetActionStateFloat()
	// AJT: TODO: xrGetActionStatePose()

	return 1;
}


uint32_t CreateDepthTexture(uint32_t colorTexture)
{
	uint32_t width, height;
	glBindTexture(GL_TEXTURE_2D, colorTexture);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	uint32_t depthTexture;
	glGenTextures(1, &depthTexture);
	glBindTexture(GL_TEXTURE_2D, depthTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0 );
	return depthTexture;
}

uint32_t GetDepthTextureFromColorTexture( uint32_t tex )
{
	int i;
	for( i = 0; i < numColorDepthPairs; i++ )
	{
		if( colorDepthPairs[i*2] == tex )
			return colorDepthPairs[i*2+1];
	}
	colorDepthPairs = realloc( colorDepthPairs, (numColorDepthPairs+1)*2*sizeof(uint32_t) );
	colorDepthPairs[numColorDepthPairs*2+0] = tex;
	return colorDepthPairs[numColorDepthPairs*2+1] = CreateDepthTexture( tex );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void InitPoseMat(float* result, const XrPosef * pose)
{
    const float x2  = pose->orientation.x + pose->orientation.x;
    const float y2  = pose->orientation.y + pose->orientation.y;
    const float z2  = pose->orientation.z + pose->orientation.z;

    const float xx2 = pose->orientation.x * x2;
    const float yy2 = pose->orientation.y * y2;
    const float zz2 = pose->orientation.z * z2;

    const float yz2 = pose->orientation.y * z2;
    const float wx2 = pose->orientation.w * x2;
    const float xy2 = pose->orientation.x * y2;
    const float wz2 = pose->orientation.w * z2;
    const float xz2 = pose->orientation.x * z2;
    const float wy2 = pose->orientation.w * y2;

    result[0] = 1.0f - yy2 - zz2;
    result[1] = xy2 + wz2;
    result[2] = xz2 - wy2;
    result[3] = 0.0f;

    result[4] = xy2 - wz2;
    result[5] = 1.0f - xx2 - zz2;
    result[6] = yz2 + wx2;
    result[7] = 0.0f;

    result[8] = xz2 + wy2;
    result[9] = yz2 - wx2;
    result[10] = 1.0f - xx2 - yy2;
    result[11] = 0.0f;

    result[12] = pose->position.x;
    result[13] = pose->position.y;
    result[14] = pose->position.z;
    result[15] = 1.0;
}

static void MultiplyMat(float* result, const float* a, const float* b)
{
    result[0] = a[0] * b[0] + a[4] * b[1] + a[8] * b[2] + a[12] * b[3];
    result[1] = a[1] * b[0] + a[5] * b[1] + a[9] * b[2] + a[13] * b[3];
    result[2] = a[2] * b[0] + a[6] * b[1] + a[10] * b[2] + a[14] * b[3];
    result[3] = a[3] * b[0] + a[7] * b[1] + a[11] * b[2] + a[15] * b[3];

    result[4] = a[0] * b[4] + a[4] * b[5] + a[8] * b[6] + a[12] * b[7];
    result[5] = a[1] * b[4] + a[5] * b[5] + a[9] * b[6] + a[13] * b[7];
    result[6] = a[2] * b[4] + a[6] * b[5] + a[10] * b[6] + a[14] * b[7];
    result[7] = a[3] * b[4] + a[7] * b[5] + a[11] * b[6] + a[15] * b[7];

    result[8] = a[0] * b[8] + a[4] * b[9] + a[8] * b[10] + a[12] * b[11];
    result[9] = a[1] * b[8] + a[5] * b[9] + a[9] * b[10] + a[13] * b[11];
    result[10] = a[2] * b[8] + a[6] * b[9] + a[10] * b[10] + a[14] * b[11];
    result[11] = a[3] * b[8] + a[7] * b[9] + a[11] * b[10] + a[15] * b[11];

    result[12] = a[0] * b[12] + a[4] * b[13] + a[8] * b[14] + a[12] * b[15];
    result[13] = a[1] * b[12] + a[5] * b[13] + a[9] * b[14] + a[13] * b[15];
    result[14] = a[2] * b[12] + a[6] * b[13] + a[10] * b[14] + a[14] * b[15];
    result[15] = a[3] * b[12] + a[7] * b[13] + a[11] * b[14] + a[15] * b[15];
}

static void InvertOrthogonalMat(float* result, float* src)
{
    result[0] = src[0];
    result[1] = src[4];
    result[2] = src[8];
    result[3] = 0.0f;
    result[4] = src[1];
    result[5] = src[5];
    result[6] = src[9];
    result[7] = 0.0f;
    result[8] = src[2];
    result[9] = src[6];
    result[10] = src[10];
    result[11] = 0.0f;
    result[12] = -(src[0] * src[12] + src[1] * src[13] + src[2] * src[14]);
    result[13] = -(src[4] * src[12] + src[5] * src[13] + src[6] * src[14]);
    result[14] = -(src[8] * src[12] + src[9] * src[13] + src[10] * src[14]);
    result[15] = 1.0f;
}

enum GraphicsAPI { GRAPHICS_VULKAN, GRAPHICS_OPENGL, GRAPHICS_OPENGL_ES, GRAPHICS_D3D };
static void InitProjectionMat(float* result, enum GraphicsAPI graphicsApi, const float tanAngleLeft,
							  const float tanAngleRight, const float tanAngleUp, float const tanAngleDown,
							  const float nearZ, const float farZ)
{
	const float tanAngleWidth = tanAngleRight - tanAngleLeft;

	// Set to tanAngleDown - tanAngleUp for a clip space with positive Y down (Vulkan).
	// Set to tanAngleUp - tanAngleDown for a clip space with positive Y up (OpenGL / D3D / Metal).
	const float tanAngleHeight = graphicsApi == GRAPHICS_VULKAN ? (tanAngleDown - tanAngleUp) : (tanAngleUp - tanAngleDown);

	// Set to nearZ for a [-1,1] Z clip space (OpenGL / OpenGL ES).
	// Set to zero for a [0,1] Z clip space (Vulkan / D3D / Metal).
	const float offsetZ = (graphicsApi == GRAPHICS_OPENGL || graphicsApi == GRAPHICS_OPENGL_ES) ? nearZ : 0;

	if (farZ <= nearZ)
	{
		// place the far plane at infinity
		result[0] = 2 / tanAngleWidth;
		result[4] = 0;
		result[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
		result[12] = 0;

		result[1] = 0;
		result[5] = 2 / tanAngleHeight;
		result[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
		result[13] = 0;

		result[2] = 0;
		result[6] = 0;
		result[10] = -1;
		result[14] = -(nearZ + offsetZ);

		result[3] = 0;
		result[7] = 0;
		result[11] = -1;
		result[15] = 0;
	}
	else
	{
		// normal projection
		result[0] = 2 / tanAngleWidth;
		result[4] = 0;
		result[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
		result[12] = 0;

		result[1] = 0;
		result[5] = 2 / tanAngleHeight;
		result[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
		result[13] = 0;

		result[2] = 0;
		result[6] = 0;
		result[10] = -(farZ + offsetZ) / (farZ - nearZ);
		result[14] = -(farZ * (nearZ + offsetZ)) / (farZ - nearZ);

		result[3] = 0;
		result[7] = 0;
		result[11] = -1;
		result[15] = 0;
	}
}


int RenderLayer(XrInstance instance, XrSession session, XrViewConfigurationView * viewConfigs, int viewConfigsCount,
				 XrSpace stageSpace, struct SwapchainInfo * swapchains,
				 XrSwapchainImageOpenGLKHR ** swapchainImages, uint32_t * swapchainLengths,
				 GLuint * colorToDepthMap, GLuint frameBuffer,
				 XrTime predictedDisplayTime,
				 XrCompositionLayerProjection * layer)
{
	XrViewState viewState;
	viewState.type = XR_TYPE_VIEW_STATE;
	viewState.next = NULL;

	uint32_t viewCapacityInput = viewConfigsCount;
	uint32_t viewCountOutput;

	XrView views[viewConfigsCount];
	for (size_t i = 0; i < viewConfigsCount; i++)
	{
		views[i].type = XR_TYPE_VIEW;
		views[i].next = NULL;
	}

	XrViewLocateInfo vli;
	vli.type = XR_TYPE_VIEW_LOCATE_INFO;
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = predictedDisplayTime;
	vli.space = stageSpace;
	XrResult result = xrLocateViews( session, &vli, &viewState, viewCapacityInput, &viewCountOutput, views );
	if (!CheckResult(instance, result, "xrLocateViews"))
	{
		return 0;
	}
	
	XrCompositionLayerProjectionView projectionLayerViews[viewCountOutput];
	memset( projectionLayerViews, 0, sizeof( XrCompositionLayerProjectionView ) * viewCountOutput );

	for (uint32_t i = 0; i < viewConfigsCount; i++)
		for (uint32_t j = 0; j < swapchainLengths[i]; j++)
		{
			const XrSwapchainImageOpenGLKHR * swapchainImage = &swapchainImages[i][j];
			// ??? Need to do this?
		}

	if (XR_UNQUALIFIED_SUCCESS(result))
	{
		// Render view to the appropriate part of the swapchain image.
		for (uint32_t i = 0; i < viewCountOutput; i++)
		{
			// Each view has a separate swapchain which is acquired, rendered to, and released.
			const struct SwapchainInfo * viewSwapchain = swapchains + i;

			XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			uint32_t swapchainImageIndex;
			result = xrAcquireSwapchainImage(viewSwapchain->handle, &ai, &swapchainImageIndex);
			if (!CheckResult(instance, result, "xrAquireSwapchainImage"))
			{
				return 0;
			}

			XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wi.timeout = XR_INFINITE_DURATION;
			result = xrWaitSwapchainImage(viewSwapchain->handle, &wi);
			if (!CheckResult(instance, result, "xrWaitSwapchainImage"))
			{
				return 0;
			}
			
			XrCompositionLayerProjectionView * layerView = projectionLayerViews + i;

			layerView->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			layerView->pose = views[i].pose;
			layerView->fov = views[i].fov;
			layerView->subImage.swapchain = viewSwapchain->handle;
			layerView->subImage.imageRect.offset.x = 0;
			layerView->subImage.imageRect.offset.y = 0;
			layerView->subImage.imageRect.extent.width = viewSwapchain->width;
			layerView->subImage.imageRect.extent.height = viewSwapchain->height;

			const XrSwapchainImageOpenGLKHR * swapchainImage = &swapchainImages[i][swapchainImageIndex];

			uint32_t colorTexture = swapchainImage->image;
			uint32_t depthTexture = GetDepthTextureFromColorTexture( colorTexture );

			minXRglBindFramebuffer( GL_FRAMEBUFFER, frameBuffer );

			glViewport(layerView->subImage.imageRect.offset.x,
					   layerView->subImage.imageRect.offset.y,
					   layerView->subImage.imageRect.extent.width,
					   layerView->subImage.imageRect.extent.height);

			minXRglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
			minXRglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

			glClearColor(0.0f, 0.1f, 0.0f, 1.0f);
			glClearDepth(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			// Render Pipeline copied from https://github.com/hyperlogic/openxrstub/blob/main/src/main.cpp

			// convert XrFovf into an OpenGL projection matrix.
			const float tanLeft = tan(layerView->fov.angleLeft);
			const float tanRight = tan(layerView->fov.angleRight);
			const float tanDown = tan(layerView->fov.angleDown);
			const float tanUp = tan(layerView->fov.angleUp);
			const float nearZ = 0.05f;
			const float farZ = 100.0f;
			float projMat[16];
			InitProjectionMat(projMat, GRAPHICS_OPENGL, tanLeft, tanRight, tanUp, tanDown, nearZ, farZ);

			// compute view matrix by inverting the pose
			float invViewMat[16];
			InitPoseMat(invViewMat, &layerView->pose);
			float viewMat[16];
			InvertOrthogonalMat(viewMat, invViewMat);

			float modelViewProjMat[16];
			MultiplyMat(modelViewProjMat, projMat, viewMat);

			glUseProgram(programInfo.program);
			glUniformMatrix4fv(programInfo.modelViewProjMatUniformLoc, 1, GL_FALSE, modelViewProjMat);
			float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
			glUniform4fv(programInfo.colorUniformLoc, 1, green);


			minXRglBindFramebuffer(GL_FRAMEBUFFER, 0);

			XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			result = xrReleaseSwapchainImage( viewSwapchain->handle, &ri );
			if (!CheckResult(instance, result, "xrReleaseSwapchainImage"))
			{
				return 0;
			}
		}

		layer->viewCount = viewCountOutput;
		layer->views = projectionLayerViews;
	}

	return 1;
}

int RenderFrame(XrInstance instance, XrSession session, XrViewConfigurationView * viewConfigs, int viewConfigsCount,
				 XrSpace stageSpace, struct SwapchainInfo * swapchains,
				 XrSwapchainImageOpenGLKHR ** swapchainImages, uint32_t * swapchainLengths,
				 GLuint * colorToDepthMap, int numColorDepthPairs, GLuint frameBuffer )
{
	XrFrameState fs;
	fs.type = XR_TYPE_FRAME_STATE;
	fs.next = NULL;

	XrFrameWaitInfo fwi;
	fwi.type = XR_TYPE_FRAME_WAIT_INFO;
	fwi.next = NULL;

	XrResult result = xrWaitFrame(session, &fwi, &fs);
	if (!CheckResult(instance, result, "xrWaitFrame"))
	{
		return 0;
	}

	XrFrameBeginInfo fbi;
	fbi.type = XR_TYPE_FRAME_BEGIN_INFO;
	fbi.next = NULL;
	result = xrBeginFrame(session, &fbi);
	if (!CheckResult(instance, result, "xrBeginFrame"))
	{
		return 0;
	}

	int layerCount = 0;
	XrCompositionLayerProjection layer;
	XrCompositionLayerBaseHeader * layers[1] = { &layer };
	layer.layerFlags = 0; //XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	layer.next = NULL;
	layer.space = stageSpace;

	if (fs.shouldRender == XR_TRUE)
	{
		if (RenderLayer(instance, session, viewConfigs, viewConfigsCount,
						stageSpace,
						swapchains,
						swapchainImages,
						swapchainLengths,
						colorToDepthMap,
						frameBuffer, fs.predictedDisplayTime, &layer))
		{
			layerCount++;
		}
	}

	XrFrameEndInfo fei = { 0 };
	fei.type = XR_TYPE_FRAME_END_INFO;
	fei.displayTime = fs.predictedDisplayTime;
	fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	fei.layerCount = layerCount;
	fei.layers = layers;

	result = xrEndFrame(session, &fei);
	if (!CheckResult(instance, result, "xrEndFrame"))
	{
		return 0;
	}

	return 1;
}

int main()
{
	if( ( numExtensions = EnumerateExtensions( &extensionProps ) ) == 0 ) return -1;
	if( !ExtensionSupported( extensionProps, numExtensions, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) )
	{
		printf("XR_KHR_opengl_enable not supported!\n");
		return 1;
	}
	if ( !CreateInstance( &instance ) ) return -1;
	if ( !GetSystemId( instance, &systemId ) ) return -1;
	if ( ( numViewConfigs = EnumerateViewConfigs(instance, systemId, &viewConfigs ) ) == 0 ) return -1;

	CNFGSetup( "Example App", 1024, 768 );
	EnumOpenGLExtensions();
	
	renderInfo.program = CNFGGLInternalLoadShader( 
		"uniform vec4 xfrm;"
		"attribute vec3 a0;"
		"attribute vec4 a1;"
		"varying vec4 vc;"
		"void main() { gl_Position = vec4( a0.xy*xfrm.xy+xfrm.zw, a0.z, 0.5 ); vc = a1; }",

		"varying vec4 vc;"
		"void main() { gl_FragColor = vec4(vc.rgba); }" 
		 );


	if ( !CreateSession(instance, systemId, &session ) ) return -1;
	if ( !CreateActions(instance, systemId, session, &actionSet ) ) return -1;
	if ( !CreateStageSpace(instance, systemId, session, &stageSpace ) ) return -1;

	minXRglGenFramebuffers(1, &frameBuffer);
	
	if (!CreateSwapchains(instance, session, viewConfigs, numViewConfigs,
						  &swapchains, &swapchainImages, &swapchainLengths )) return -1;
	
	// numColorDepthPairs * 2 
	//GLuint * colorDepthPairs;
	//int numColorDepthPairs;

	int sessionReady = 0;
	XrSessionState xrState = XR_SESSION_STATE_UNKNOWN;

	while ( CNFGHandleInput() )
	{
		XrEventDataBuffer xrEvent;
		xrEvent.type = XR_TYPE_EVENT_DATA_BUFFER;
		xrEvent.next = NULL;

		XrResult result = xrPollEvent(instance, &xrEvent);

		if (result == XR_SUCCESS)
		{
			switch (xrEvent.type)
			{
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				// Receiving the XrEventDataInstanceLossPending event structure indicates that the application is about to lose the indicated XrInstance at the indicated lossTime in the future.
				// The application should call xrDestroyInstance and relinquish any instance-specific resources.
				// This typically occurs to make way for a replacement of the underlying runtime, such as via a software update.
				printf("xrEvent: XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING\n");
				break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			{
				// Receiving the XrEventDataSessionStateChanged event structure indicates that the application has changed lifecycle stat.e
				printf("xrEvent: XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED -> ");
				XrEventDataSessionStateChanged* ssc = (XrEventDataSessionStateChanged*)&xrEvent;
				xrState = ssc->state;
				switch (xrState)
				{
				case XR_SESSION_STATE_IDLE:
					// The initial state after calling xrCreateSession or returned to after calling xrEndSession.
					printf("XR_SESSION_STATE_IDLE\n");
					break;
				case XR_SESSION_STATE_READY:
					// The application is ready to call xrBeginSession and sync its frame loop with the runtime.
					printf("XR_SESSION_STATE_READY\n");
					if (!BeginSession(instance, systemId, session))
					{
						return 1;
					}
					sessionReady = 1;
					break;
				case XR_SESSION_STATE_SYNCHRONIZED:
					// The application has synced its frame loop with the runtime but is not visible to the user.
					printf("XR_SESSION_STATE_SYNCHRONIZED\n");
					break;
				case XR_SESSION_STATE_VISIBLE:
					// The application has synced its frame loop with the runtime and is visible to the user but cannot receive XR input.
					printf("XR_SESSION_STATE_VISIBLE\n");
					break;
				case XR_SESSION_STATE_FOCUSED:
					// The application has synced its frame loop with the runtime, is visible to the user and can receive XR input.
					printf("XR_SESSION_STATE_FOCUSED\n");
					break;
				case XR_SESSION_STATE_STOPPING:
					// The application should exit its frame loop and call xrEndSession.
					printf("XR_SESSION_STATE_STOPPING\n");
					break;
				case XR_SESSION_STATE_LOSS_PENDING:
					printf("XR_SESSION_STATE_LOSS_PENDING\n");
					// The session is in the process of being lost. The application should destroy the current session and can optionally recreate it.
					break;
				case XR_SESSION_STATE_EXITING:
					printf("XR_SESSION_STATE_EXITING\n");
					// The application should end its XR experience and not automatically restart it.
					break;
				default:
					printf("XR_SESSION_STATE_??? %d\n", (int)xrState);
					break;
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
				// The XrEventDataReferenceSpaceChangePending event is sent to the application to notify it that the origin (and perhaps the bounds) of a reference space is changing.
				printf("XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING\n");
				break;
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				// Receiving the XrEventDataEventsLost event structure indicates that the event queue overflowed and some events were removed at the position within the queue at which this event was found.
				printf("xrEvent: XR_TYPE_EVENT_DATA_EVENTS_LOST\n");
				break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				// The XrEventDataInteractionProfileChanged event is sent to the application to notify it that the active input form factor for one or more top level user paths has changed.:
				printf("XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED\n");
				break;
			default:
				printf("Unhandled event type %d\n", xrEvent.type);
				break;
			}
		}

		if (sessionReady)
		{
			if (!SyncInput(instance, session, actionSet))
			{
				return -1;
			}

			if (!RenderFrame(instance, session, viewConfigs, numViewConfigs,
							 stageSpace, swapchains, swapchainImages, swapchainLengths,
							 colorDepthPairs, numColorDepthPairs, frameBuffer ) )
			{
				return -1;
			}
		}
		else
		{
			OGUSleep(100000);
		}
	}

	XrResult result;
	int i;
	for( i = 0; i < numViewConfigs; i++ )
	{
		result = xrDestroySwapchain(swapchains[i].handle);
		CheckResult(instance, result, "xrDestroySwapchain");
	}

	result = xrDestroySpace(stageSpace);
	CheckResult(instance, result, "xrDestroySpace");

	result = xrEndSession(session);
	CheckResult(instance, result, "xrEndSession");

	result = xrDestroySession(session);
	CheckResult(instance, result, "xrDestroySession");

	result = xrDestroyInstance(instance);
	CheckResult(XR_NULL_HANDLE, result, "xrDestroyInstance");

	return 0;
}

//For rawdraw (we don't use this)
void HandleKey( int keycode, int bDown ) { }
void HandleButton( int x, int y, int button, int bDown ) { }
void HandleMotion( int x, int y, int mask ) { }
void HandleDestroy() { }
