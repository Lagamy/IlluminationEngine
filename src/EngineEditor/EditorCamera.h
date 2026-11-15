#pragma once

#ifdef EDITOR
#include "Camera.h"
#include <GLFW/glfw3.h>

/*
The editor's free camera (Editor::camera). It is a Camera (so Renderer::runAll draws it) plus the controls, which work with the cursor visible:
W A S D / Space / Ctrl fly (Shift = full speed: speedUp times speed, without the zoomScale slowdown); holding the right mouse button turns it and holding the middle one drags it sideways and up/down (pan); the cursor is hidden and captured for
the hold, so the drag has no screen edge; the wheel moves it along the look direction. All of this is scaled by zoomScale, which follows the distance to the selected entity, else to the model the camera looks at (updateSpeedScale): close to it the camera is slow, far from everything it is fast.
The mouse controls start only on a point the debug UI does not cover.
*/
struct EditorCamera : Camera
{
	/* Pan distance per mouse pixel and zoom distance per wheel notch, as a fraction of speed (units per second), so they follow the speed the wheel sets. Shift makes the zoom speedUp times speed, unscaled. */
	/* Degrees of turn per mouse pixel (Camera::processMouse scales by the gameplay camera's Globals::Mouse::mouseSensitivity, so onMouseMove corrects for that). */
	float lookSensitivity = 0.05f;

	float panPerSpeed = 0.002f;
	float zoomPerSpeed = 0.1f;

	/* zoomScale is the distance to the nearest entity divided by referenceDistance (the distance at which the speed settings apply as they are), kept between minZoomScale and maxZoomScale. */
	float referenceDistance = 10.0f;
	float minZoomScale = 0.001f;
	float maxZoomScale = 10.0f;

	/* Multiplies speed in the fly keys, pan and zoom; set by updateSpeedScale, not a setting (not shown, not saved). */
	float zoomScale = 1.0f;
	float minSpeed = 1.0f;
	float maxSpeed = 100000.0f;

	/* True from a button press until its release; panning tells which one: middle = pan, right = turn. */
	bool dragging = false;
	bool panning = false;
	/* The right button box-selects (the Geometry, Texturing and Collision subtabs; Editor::processInputs sets it each frame): it starts the look hold only with Alt held. A look already started goes on until the button is let go. */
	bool rightSelects = false;
	/* True after alignTo (keys 1-6 in the Geometry, Texturing and Collision subtabs) until a look hold turns the camera; while it is true a left drag pans the camera in the view plane (GeometryPanel::update). */
	bool aligned = false;
	/* The middle button turns the camera around orbitCenter instead of panning it (Editor::processInputs sets both while the Geometry or Texturing subtab is shown, orbitCenter = the middle of the open mesh); read when the hold starts. orbiting is true during such a hold. */
	bool middleOrbits = false;
	glm::vec3 orbitCenter = glm::vec3(0.0f);
	bool orbiting = false;

	/* Looks along direction_ (a unit vector) and, with a target_, moves along that line so that the target_ stays at the same distance (at least 1) in the middle of the view. Sets aligned and the orthographic projection (Camera::orthographic, as tall as the perspective view is at the target; the wheel zooms it). */
	void alignTo(const glm::vec3& rDirection_, const glm::vec3* pTarget_);

	/* Turns the camera by a mouse movement in pixels (as the look does) about orbitCenter, keeping its distance to it; also ends the aligned view. */
	void orbit(float xOffset_, float yOffset_);

	/* Ends the aligned view: the perspective projection again. */
	void leaveAligned();

	/* Moves the camera along its own right and up axes by pixels (the middle button pan; y is positive downwards). */
	void panPixels(float xOffset_, float yOffset_);

	/* The fly keys, scaled by the frame time. */
	void processKeys(GLFWwindow* pWindow_, float deltaTime_);

	/* Starts or ends the button hold (call once per frame); a press the UI had the mouse at does not start one. Returns whether dragging changed. */
	bool updateDrag(GLFWwindow* pWindow_, bool uiWantsMouse_);

	/* A mouse movement in pixels: turns the camera (right button) or pans it (middle); nothing unless dragging. */
	void onMouseMove(GLFWwindow* pWindow_, float xOffset_, float yOffset_);

	/* Moves zoomScale towards nearestDistance_ / referenceDistance (call once per frame with the distance to the model the camera looks at); it eases there, so passing a large object does not make the speed jump. */
	void updateSpeedScale(float nearestDistance_, float deltaTime_);

	/* A wheel movement (GLFW yoffset): moves along the look direction. */
	void onScroll(GLFWwindow* pWindow_, float yOffset_);

	EditorCamera(glm::vec3 position_) : Camera(position_) {}
	EditorCamera() = default;
};
#endif
