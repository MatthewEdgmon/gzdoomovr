// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2015 Christopher Bruns
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl_stereo3d.cpp
** Stereoscopic 3D API
**
*/

#include "gl_system.h"
#include "gl_renderer.h"
#include "gl_renderbuffers.h"
#include "hw_vrmodes.h"
#include "gl_framebuffer.h"
#include "gl_postprocessstate.h"
#include "gl_framebuffer.h"
#include "gl_shaderprogram.h"
#include "gl_buffers.h"
#include "templates.h"

#ifdef OPENVR_SUPPORT
#include "c_console.h"
#include <iostream>
#include <openvr.h>
#include <sstream>
#endif /* OPENVR_SUPPORT */

EXTERN_CVAR(Int, vr_mode)
EXTERN_CVAR(Float, vid_saturation)
EXTERN_CVAR(Float, vid_brightness)
EXTERN_CVAR(Float, vid_contrast)
EXTERN_CVAR(Int, gl_satformula)
EXTERN_CVAR(Int, gl_dither_bpc)

void UpdateVRModes(bool considerQuadBuffered = true);

namespace OpenGLRenderer
{

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentAnaglyph(bool r, bool g, bool b)
{
	mBuffers->BindOutputFB();
	ClearBorders();

	glColorMask(r, g, b, 1);
	mBuffers->BindEyeTexture(0, 0);
 	DrawPresentTexture(screen->mOutputLetterbox, true);

	glColorMask(!r, !g, !b, 1);
	mBuffers->BindEyeTexture(1, 0);
	DrawPresentTexture(screen->mOutputLetterbox, true);

	glColorMask(1, 1, 1, 1);
}

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentSideBySide(int vrmode)
{
	if (vrmode == VR_SIDEBYSIDEFULL || vrmode == VR_SIDEBYSIDESQUISHED)
	{
		mBuffers->BindOutputFB();
		ClearBorders();

		// Compute screen regions to use for left and right eye views
		int leftWidth = screen->mOutputLetterbox.width / 2;
		int rightWidth = screen->mOutputLetterbox.width - leftWidth;
		IntRect leftHalfScreen = screen->mOutputLetterbox;
		leftHalfScreen.width = leftWidth;
		IntRect rightHalfScreen = screen->mOutputLetterbox;
		rightHalfScreen.width = rightWidth;
		rightHalfScreen.left += leftWidth;

		mBuffers->BindEyeTexture(0, 0);
		DrawPresentTexture(leftHalfScreen, true);

		mBuffers->BindEyeTexture(1, 0);
		DrawPresentTexture(rightHalfScreen, true);
	}
	else if (vrmode == VR_SIDEBYSIDELETTERBOX)
	{
		mBuffers->BindOutputFB();
		screen->mOutputLetterbox.top = screen->mOutputLetterbox.height;

		ClearBorders();
		screen->mOutputLetterbox.top = 0;  //reset so screenshots can be taken

		// Compute screen regions to use for left and right eye views
		int leftWidth = screen->mOutputLetterbox.width / 2;
		int rightWidth = screen->mOutputLetterbox.width - leftWidth;
		//cut letterbox height in half
		int height = screen->mOutputLetterbox.height / 2;
		int top = height * .5;
		IntRect leftHalfScreen = screen->mOutputLetterbox;
		leftHalfScreen.width = leftWidth;
		leftHalfScreen.height = height;
		leftHalfScreen.top = top;
		IntRect rightHalfScreen = screen->mOutputLetterbox;
		rightHalfScreen.width = rightWidth;
		rightHalfScreen.left += leftWidth;
		//give it those cinematic black bars on top and bottom
		rightHalfScreen.height = height;
		rightHalfScreen.top = top;

		mBuffers->BindEyeTexture(0, 0);
		DrawPresentTexture(leftHalfScreen, true);

		mBuffers->BindEyeTexture(1, 0);
		DrawPresentTexture(rightHalfScreen, true);
	}
}


//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentTopBottom()
{
	mBuffers->BindOutputFB();
	ClearBorders();

	// Compute screen regions to use for left and right eye views
	int topHeight = screen->mOutputLetterbox.height / 2;
	int bottomHeight = screen->mOutputLetterbox.height - topHeight;
	IntRect topHalfScreen = screen->mOutputLetterbox;
	topHalfScreen.height = topHeight;
	topHalfScreen.top = topHeight;
	IntRect bottomHalfScreen = screen->mOutputLetterbox;
	bottomHalfScreen.height = bottomHeight;
	bottomHalfScreen.top = 0;

	mBuffers->BindEyeTexture(0, 0);
	DrawPresentTexture(topHalfScreen, true);

	mBuffers->BindEyeTexture(1, 0);
	DrawPresentTexture(bottomHalfScreen, true);
}

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::prepareInterleavedPresent(FPresentShaderBase& shader)
{
	mBuffers->BindOutputFB();
	ClearBorders();


	// Bind each eye texture, for composition in the shader
	mBuffers->BindEyeTexture(0, 0);
	mBuffers->BindEyeTexture(1, 1);

	glActiveTexture(GL_TEXTURE0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glActiveTexture(GL_TEXTURE1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	const IntRect& box = screen->mOutputLetterbox;
	glViewport(box.left, box.top, box.width, box.height);

	shader.Bind();

	if (framebuffer->IsHWGammaActive())
	{
		shader.Uniforms->InvGamma = 1.0f;
		shader.Uniforms->Contrast = 1.0f;
		shader.Uniforms->Brightness = 0.0f;
		shader.Uniforms->Saturation = 1.0f;
	}
	else
	{
		shader.Uniforms->InvGamma = 1.0f / clamp<float>(vid_gamma, 0.1f, 4.f);
		shader.Uniforms->Contrast = clamp<float>(vid_contrast, 0.1f, 3.f);
		shader.Uniforms->Brightness = clamp<float>(vid_brightness, -0.8f, 0.8f);
		shader.Uniforms->Saturation = clamp<float>(vid_saturation, -15.0f, 15.0f);
		shader.Uniforms->GrayFormula = static_cast<int>(gl_satformula);
	}
	shader.Uniforms->HdrMode = 0;
	shader.Uniforms->ColorScale = (gl_dither_bpc == -1) ? 255.0f : (float)((1 << gl_dither_bpc) - 1);
	shader.Uniforms->Scale = {
		screen->mScreenViewport.width / (float)mBuffers->GetWidth(),
		screen->mScreenViewport.height / (float)mBuffers->GetHeight()
	};
	shader.Uniforms->Offset = { 0.0f, 0.0f };
	shader.Uniforms.SetData();
	static_cast<GLDataBuffer*>(shader.Uniforms.GetBuffer())->BindBase();
}

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentColumnInterleaved()
{
	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);
	prepareInterleavedPresent(*mPresent3dColumnShader);

	// Compute absolute offset from top of screen to top of current display window
	// because we need screen-relative, not window-relative, scan line parity

	// Todo:
	//auto clientoffset = screen->GetClientOffset();
	//auto windowHOffset = clientoffset.X % 2;
	int windowHOffset = 0;

	mPresent3dColumnShader->Uniforms->WindowPositionParity = windowHOffset;
	mPresent3dColumnShader->Uniforms.SetData();
	static_cast<GLDataBuffer*>(mPresent3dColumnShader->Uniforms.GetBuffer())->BindBase();

	RenderScreenQuad();
}

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentRowInterleaved()
{
	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);
	prepareInterleavedPresent(*mPresent3dRowShader);

	// Todo:
	//auto clientoffset = screen->GetClientOffset();
	//auto windowVOffset = clientoffset.Y % 2;
	int windowVOffset = 0;

	mPresent3dRowShader->Uniforms->WindowPositionParity =
		(windowVOffset
			+ screen->mOutputLetterbox.height + 1 // +1 because of origin at bottom
			) % 2;

	mPresent3dRowShader->Uniforms.SetData();
	static_cast<GLDataBuffer*>(mPresent3dRowShader->Uniforms.GetBuffer())->BindBase();
	RenderScreenQuad();
}

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentCheckerInterleaved()
{
	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);
	prepareInterleavedPresent(*mPresent3dCheckerShader);

	// Compute absolute offset from top of screen to top of current display window
	// because we need screen-relative, not window-relative, scan line parity

	//auto clientoffset = screen->GetClientOffset();
	//auto windowHOffset = clientoffset.X % 2;
	//auto windowVOffset = clientoffset.Y % 2;
	int windowHOffset = 0;
	int windowVOffset = 0;

	mPresent3dCheckerShader->Uniforms->WindowPositionParity =
		(windowVOffset
			+ windowHOffset
			+ screen->mOutputLetterbox.height + 1 // +1 because of origin at bottom
			) % 2; // because we want the top pixel offset, but gl_FragCoord.y is the bottom pixel offset

	mPresent3dCheckerShader->Uniforms.SetData();
	static_cast<GLDataBuffer*>(mPresent3dCheckerShader->Uniforms.GetBuffer())->BindBase();
	RenderScreenQuad();
}

//==========================================================================
//
// Sometimes the stereo render context is not ready immediately at start up
//
//==========================================================================

bool FGLRenderer::QuadStereoCheckInitialRenderContextState()
{
	// Keep trying until we see at least one good OpenGL context to render to
	bool bQuadStereoSupported = false;
	bool bDecentContextWasFound = false;
	int contextCheckCount = 0;
	if ((!bDecentContextWasFound) && (contextCheckCount < 200))
	{
		contextCheckCount += 1;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);  // This question is about the main screen display context
		GLboolean supportsStereo, supportsBuffered;
		glGetBooleanv(GL_DOUBLEBUFFER, &supportsBuffered);
		if (supportsBuffered) // Finally, a useful OpenGL context
		{
			// This block will be executed exactly ONCE during a game run
			bDecentContextWasFound = true; // now we can stop checking every frame...
										   // Now check whether this context supports hardware stereo
			glGetBooleanv(GL_STEREO, &supportsStereo);
			bQuadStereoSupported = supportsStereo && supportsBuffered;
			if (! bQuadStereoSupported)
				UpdateVRModes(false);
		}
	}
	return bQuadStereoSupported;
}

//==========================================================================
//
//
//
//==========================================================================

void FGLRenderer::PresentQuadStereo()
{
	if (QuadStereoCheckInitialRenderContextState())
	{
		mBuffers->BindOutputFB();

		glDrawBuffer(GL_BACK_LEFT);
		ClearBorders();
		mBuffers->BindEyeTexture(0, 0);
		DrawPresentTexture(screen->mOutputLetterbox, true);

		glDrawBuffer(GL_BACK_RIGHT);
		ClearBorders();
		mBuffers->BindEyeTexture(1, 0);
		DrawPresentTexture(screen->mOutputLetterbox, true);

		glDrawBuffer(GL_BACK);
	}
	else
	{
		mBuffers->BindOutputFB();
		ClearBorders();
		mBuffers->BindEyeTexture(0, 0);
		DrawPresentTexture(screen->mOutputLetterbox, true);
	}
}

//==========================================================================
//
//
//
//==========================================================================

#ifdef OPENVR_SUPPORT

vr::HmdVector3d_t FGLRenderer::EulerAnglesFromQuaternion(vr::HmdQuaternion_t quaternion)
{
	double q0 = quaternion.w;
	// permute axes to make "Y" up/yaw
	double q2 = quaternion.x;
	double q3 = quaternion.y;
	double q1 = quaternion.z;

	// http://stackoverflow.com/questions/18433801/converting-a-3x3-matrix-to-euler-tait-bryan-angles-pitch-yaw-roll
	double roll = atan2(2 * (q0 * q1 + q2 * q3), 1 - 2 * (q1 * q1 + q2 * q2));
	double pitch = asin(2 * (q0 * q2 - q3 * q1));
	double yaw = atan2(2 * (q0 * q3 + q1 * q2), 1 - 2 * (q2 * q2 + q3 * q3));

	return vr::HmdVector3d_t{ yaw, pitch, roll };
}

vr::HmdVector3d_t FGLRenderer::EulerAnglesFromMatrix(vr::HmdMatrix34_t matrix)
{
	return EulerAnglesFromQuaternion(QuaternionFromMatrix(matrix));
}

vr::HmdQuaternion_t FGLRenderer::QuaternionFromMatrix(vr::HmdMatrix34_t matrix)
{
	vr::HmdQuaternion_t q;
	typedef float f34[3][4];
	f34& a = matrix.m;
	// http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
	float trace = a[0][0] + a[1][1] + a[2][2]; // I removed + 1.0f; see discussion with Ethan
	if (trace > 0) {// I changed M_EPSILON to 0
		float s = 0.5f / sqrtf(trace + 1.0f);
		q.w = 0.25f / s;
		q.x = (a[2][1] - a[1][2]) * s;
		q.y = (a[0][2] - a[2][0]) * s;
		q.z = (a[1][0] - a[0][1]) * s;
	}
	else {
		if (a[0][0] > a[1][1] && a[0][0] > a[2][2]) {
			float s = 2.0f * sqrtf(1.0f + a[0][0] - a[1][1] - a[2][2]);
			q.w = (a[2][1] - a[1][2]) / s;
			q.x = 0.25f * s;
			q.y = (a[0][1] + a[1][0]) / s;
			q.z = (a[0][2] + a[2][0]) / s;
		}
		else if (a[1][1] > a[2][2]) {
			float s = 2.0f * sqrtf(1.0f + a[1][1] - a[0][0] - a[2][2]);
			q.w = (a[0][2] - a[2][0]) / s;
			q.x = (a[0][1] + a[1][0]) / s;
			q.y = 0.25f * s;
			q.z = (a[1][2] + a[2][1]) / s;
		}
		else {
			float s = 2.0f * sqrtf(1.0f + a[2][2] - a[0][0] - a[1][1]);
			q.w = (a[1][0] - a[0][1]) / s;
			q.x = (a[0][2] + a[2][0]) / s;
			q.y = (a[1][2] + a[2][1]) / s;
			q.z = 0.25f * s;
		}
	}

	return q;
}

void FGLRenderer::InitializeOpenVR()
{
	if(vr::VR_IsHmdPresent())
	{
		// Set up IVRSystem.
		vr::EVRInitError error = vr::VRInitError_None;

		mVRSystem = vr::VR_Init(&error, vr::VRApplication_Scene);

		if(error != vr::EVRInitError::VRInitError_None)
		{
			std::string error_symbol = vr::VR_GetVRInitErrorAsSymbol(error);
			std::string error_message = vr::VR_GetVRInitErrorAsEnglishDescription(error);
			mVRSystem = nullptr;
			std::cout << "[OpenVR]: " << error_symbol << " " << error_message << std::endl;
			return;
		}

		// Setup compositor.
		if(!vr::VRCompositor())
		{
			return;
		}

		mVRSystem->GetRecommendedRenderTargetSize(&mVRSceneWidth, &mVRSceneHeight);

		// Initialize eyes.
		InitializeEye(vr::EVREye::Eye_Left);
		InitializeEye(vr::EVREye::Eye_Right);

		mIsOpenVRStarted = true;
	}
}

void FGLRenderer::InitializeEye(vr::EVREye eye)
{
	GLuint handle;
	glGenTextures(1, &handle);
	glBindTexture(GL_TEXTURE_2D, handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, screen->mSceneViewport.width, screen->mSceneViewport.height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

	mEyeTextures[eye].handle = (void*)(uintptr_t)handle;
	mEyeTextures[eye].eType = vr::TextureType_OpenGL;
	mEyeTextures[eye].eColorSpace = vr::ColorSpace_Linear;
}

void FGLRenderer::PresentEyeFrame(vr::EVREye eye)
{
	if(vr::VRCompositor() == nullptr)
	{
		return;
	}

	mBuffers->BindEyeFB(eye, true);

	// Create handle if it hasn't been created.
	if(mEyeTextures[eye].handle == nullptr)
	{
		GLuint handle;
		glGenTextures(1, &handle);
		mEyeTextures[eye].handle = (void*)(uintptr_t) handle;
		glBindTexture(GL_TEXTURE_2D, handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, screen->mSceneViewport.width, screen->mSceneViewport.height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	}

	glBindTexture(GL_TEXTURE_2D, (GLuint) mEyeTextures[eye].handle);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, screen->mSceneViewport.width, screen->mSceneViewport.height, 0);

	vr::EVRCompositorError error = vr::VRCompositor()->Submit(vr::EVREye(eye), &mEyeTextures[eye]);

	if(error != vr::EVRCompositorError::VRCompositorError_None)
	{
		std::ostringstream error_message;
		error_message << "[OpenVR]: VRCompositor()->Submit() EVRCompositorError = " << error << std::endl;
		AddToConsole(0, error_message.str().c_str());
		return;
	}
}

void FGLRenderer::PresentOpenVR()
{
	// Check if we've run OpenVR initialization at least once.
	if(mIsOpenVRStarted != true)
	{
		InitializeOpenVR();
		return;
	}

	// Render to desktop.
	if(mRenderDesktopView == true) {
		PresentSideBySide(VR_SIDEBYSIDEFULL);
	}

	// Render to HMD, but only if OpenVR is running.
	if(mRenderHMDView == true && mIsOpenVRStarted == true) {
		PresentEyeFrame(vr::EVREye::Eye_Left);
		PresentEyeFrame(vr::EVREye::Eye_Right);

		// Handle poses.
		vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
		vr::EVRCompositorError pose_error = vr::VRCompositor()->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);

		if (pose_error != vr::EVRCompositorError::VRCompositorError_None)
		{
			std::ostringstream error_message;
			error_message << "[OpenVR]: VRCompositor()->WaitGetPoses() EVRCompositorError " << pose_error << std::endl;
			AddToConsole(0, error_message.str().c_str());
			return;
		}

		vr::TrackedDevicePose_t& poseHMD = poses[vr::k_unTrackedDeviceIndex_Hmd];

		if(poseHMD.bPoseIsValid)
		{
			const vr::HmdMatrix34_t& hmdPose = poseHMD.mDeviceToAbsoluteTracking;
		}
	}
}

#endif /* OPENVR_SUPPORT */

void FGLRenderer::PresentStereo()
{
	auto vrmode = VRMode::GetVRMode(true);
	const int eyeCount = vrmode->mEyeCount;
	// Don't invalidate the bound framebuffer (..., false)
	if (eyeCount > 1)
		mBuffers->BlitToEyeTexture(mBuffers->CurrentEye(), false);

	switch (vr_mode)
	{
	default:
		return;

	case VR_GREENMAGENTA:
		PresentAnaglyph(false, true, false);
		break;

	case VR_REDCYAN:
		PresentAnaglyph(true, false, false);
		break;

	case VR_AMBERBLUE:
		PresentAnaglyph(true, true, false);
		break;

	case VR_SIDEBYSIDEFULL:
	case VR_SIDEBYSIDESQUISHED:
	case VR_SIDEBYSIDELETTERBOX:
		PresentSideBySide(vr_mode);
		break;

#ifdef OPENVR_SUPPORT
	case VR_OPENVR:
		PresentOpenVR();
		break;
#endif /* OPENVR_SUPPORT */

	case VR_TOPBOTTOM:
		PresentTopBottom();
		break;

	case VR_ROWINTERLEAVED:
		PresentRowInterleaved();
		break;

	case VR_COLUMNINTERLEAVED:
		PresentColumnInterleaved();
		break;

	case VR_CHECKERINTERLEAVED:
		PresentCheckerInterleaved();
		break;

	case VR_QUADSTEREO:
		PresentQuadStereo();
		break;
	}
}

}
