#include "EditorCamera.h"
#include "Globals.h"
#include <algorithm>
#include <cmath>

void EditorCamera::processKeys(GLFWwindow* pWindow_, float deltaTime_)
{
	/* Shift is full speed: speedUp times speed, without the zoomScale slowdown near things. */
	float distance = this->speed * this->zoomScale * deltaTime_;
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		distance = this->speed * this->speedUp * deltaTime_;
	if(glfwGetKey(pWindow_, GLFW_KEY_W) == GLFW_PRESS)
		this->move(this->front * distance);
	if(glfwGetKey(pWindow_, GLFW_KEY_S) == GLFW_PRESS)
		this->move(-this->front * distance);

	glm::vec3 right = glm::normalize(glm::cross(this->front, this->up));
	if(glfwGetKey(pWindow_, GLFW_KEY_A) == GLFW_PRESS)
		this->move(-right * distance);
	if(glfwGetKey(pWindow_, GLFW_KEY_D) == GLFW_PRESS)
		this->move(right * distance);

	if(glfwGetKey(pWindow_, GLFW_KEY_SPACE) == GLFW_PRESS)
		this->move(glm::vec3(0.0f, 1.0f, 0.0f) * distance);
	/* Alt flies down (not Ctrl: Ctrl is the shortcut key and the Geometry subtabs' merge key). */
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS)
		this->move(-glm::vec3(0.0f, 1.0f, 0.0f) * distance);
}

bool EditorCamera::updateDrag(GLFWwindow* pWindow_, bool uiWantsMouse_)
{
	bool rightDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool middleDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
	bool altDown = glfwGetKey(pWindow_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
	bool rightStarts = rightDown && (!this->rightSelects || altDown);
	bool down = this->dragging ? (this->panning ? middleDown : rightDown) : (rightStarts || middleDown);
	if(down && !this->dragging && !uiWantsMouse_)
	{
		this->panning = !rightStarts;
		this->orbiting = this->panning && this->middleOrbits;
		if(this->orbiting) this->leaveAligned();
		/* A look hold turns the camera, so it is no longer aligned. */
		if(rightStarts) this->leaveAligned();
		/* The cursor is hidden and captured for the hold; the next position is the baseline so the camera does not jump (mouseCallback, firstMouse). */
		this->dragging = true;
		glfwSetInputMode(pWindow_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		Globals::Mouse::firstMouse = true;
		return true;
	}
	if(!down && this->dragging)
	{
		this->dragging = false;
		glfwSetInputMode(pWindow_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		return true;
	}
	return false;
}

void EditorCamera::leaveAligned()
{
	bool wasOrthographic = this->orthographic;
	this->aligned = false;
	this->orthographic = false;
	if(wasOrthographic) this->refreshProjection();
}

void EditorCamera::panPixels(float xOffset_, float yOffset_)
{
	/* Pan: the scene follows the cursor, so the camera goes the opposite way along its own right and up axes. */
	glm::vec3 right = glm::normalize(glm::cross(this->front, this->up));
	glm::vec3 cameraUp = glm::cross(right, this->front);
	if(this->orthographic)
	{
		/* The picture follows the cursor exactly: a pixel is orthoHeight / the view's height in the world. */
		float perPixel = this->orthoHeight / static_cast<float>(std::max(this->viewportSize().y, 1u));
		this->move((-right * xOffset_ + cameraUp * yOffset_) * perPixel);
		return;
	}
	this->move((-right * xOffset_ + cameraUp * yOffset_) * this->panPerSpeed * this->speed * this->zoomScale);
}

void EditorCamera::orbit(float xOffset_, float yOffset_)
{
	/*
	The look turns the view (processMouse); the camera's offset from the centre is turned by the same two turns, the yaw about the world's up axis and the pitch about the camera's right axis as it is after the yaw, so the centre stays where it was in the view and the distance doesn't change.
	*/
	float scale = this->lookSensitivity / Globals::Mouse::mouseSensitivity;
	float yawBefore = this->yaw;
	float pitchBefore = this->pitch;
	glm::vec3 right = glm::normalize(glm::cross(this->front, this->up));
	this->processMouse(xOffset_ * scale, yOffset_ * scale);
	glm::mat4 yawTurn = glm::rotate(glm::mat4(1.0f), -glm::radians(this->yaw - yawBefore), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::vec3 pitchAxis = glm::vec3(yawTurn * glm::vec4(right, 0.0f));
	glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(this->pitch - pitchBefore), pitchAxis) * yawTurn;
	this->set(this->orbitCenter + glm::vec3(turn * glm::vec4(this->position - this->orbitCenter, 0.0f)));
	this->updateView();
	this->updateUBO();
}

void EditorCamera::alignTo(const glm::vec3& rDirection_, const glm::vec3* pTarget_)
{
	float distance = pTarget_ != nullptr ? std::max(glm::distance(this->position, *pTarget_), 1.0f) : 0.0f;
	/* The pitch stops just short of straight up or down (processMouse's limit): the camera's right axis is the cross product with the world up, which needs a sideways part. */
	this->pitch = std::clamp(glm::degrees(std::asin(rDirection_.y)), -89.0f, 89.0f);
	if(std::abs(rDirection_.x) > 1e-4f || std::abs(rDirection_.z) > 1e-4f) this->yaw = glm::degrees(std::atan2(rDirection_.z, rDirection_.x));
	glm::vec3 direction;
	direction.x = std::cos(glm::radians(this->yaw)) * std::cos(glm::radians(this->pitch));
	direction.y = std::sin(glm::radians(this->pitch));
	direction.z = std::sin(glm::radians(this->yaw)) * std::cos(glm::radians(this->pitch));
	this->front = glm::normalize(direction);
	if(pTarget_ != nullptr) this->set(*pTarget_ - this->front * distance);
	this->updateView();
	/* The view is as tall as the perspective one at the target; with no target the height it had stays (or the reference distance's). */
	if(pTarget_ != nullptr) this->orthoHeight = 2.0f * distance * std::tan(glm::radians(this->fov) * 0.5f);
	else if(!this->orthographic) this->orthoHeight = 2.0f * this->referenceDistance * std::tan(glm::radians(this->fov) * 0.5f);
	this->orthographic = true;
	this->refreshProjection();
	this->aligned = true;
}

void EditorCamera::onMouseMove(GLFWwindow* pWindow_, float xOffset_, float yOffset_)
{
	if(!this->dragging) return;
	if(this->panning)
	{
		if(this->orbiting) this->orbit(xOffset_, yOffset_);
		else this->panPixels(xOffset_, yOffset_);
		return;
	}

	/* processMouse multiplies by the gameplay sensitivity; the offsets are scaled so that the result is lookSensitivity degrees per pixel. */
	float scale = this->lookSensitivity / Globals::Mouse::mouseSensitivity;
	this->processMouse(xOffset_ * scale, yOffset_ * scale);
}

/* How fast zoomScale closes on its target, per second (the gap shrinks by 1 - e^-rate each second; a guess, fast enough to follow the camera and slow enough to hide jumps). */
constexpr float SpeedScaleRate = 8.0f;

/* Eased in log space, so a change feels the same at any scale. */
void EditorCamera::updateSpeedScale(float nearestDistance_, float deltaTime_)
{
	float target = glm::clamp(nearestDistance_ / this->referenceDistance, this->minZoomScale, this->maxZoomScale);
	this->zoomScale *= std::pow(target / this->zoomScale, 1.0f - std::exp(-SpeedScaleRate * deltaTime_));
}

void EditorCamera::onScroll(GLFWwindow* pWindow_, float yOffset_)
{
	/* Moving along the look direction changes nothing in an orthographic view: the wheel scales the view (10% a notch) instead. */
	if(this->orthographic)
	{
		this->orthoHeight = std::max(this->orthoHeight * std::pow(0.9f, yOffset_), 0.01f);
		this->refreshProjection();
		return;
	}
	float step = this->zoomPerSpeed * this->speed * this->zoomScale;
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		step = this->zoomPerSpeed * this->speed * this->speedUp;
	this->move(this->front * yOffset_ * step);
}
