// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Framework/Layout/InertialScrollManager.h>
#include <Framework/Layout/Overscroll.h>

/**
 * Encapsulates all scroll state: offset, target, inertia, overscroll, and pointer panning
 * for a virtual flow view.
 *
 * The controller owns the physics simulation but does NOT own layout-aware decisions
 * like snap-to-item or content height. The owning SVirtualFlowView orchestrates those
 * by querying the controller's state and calling its methods with the appropriate context.
 *
 * Ownership contract:
 *   - State: OffsetPx, TargetOffsetPx, InertialScrollManager, Overscroll, pointer panning,
 *            snap step tracking.
 *   - Physics: Ticks inertia and overscroll, applies scroll deltas with overscroll handling.
 *   - Orchestration: The parent view decides WHEN to scroll, WHAT the max offset is,
 *                    and WHERE snap targets lie. This controller handles HOW the scroll moves.
 */
class FVirtualFlowScrollController
{
public:
	// --- Offset access ---
	
	float GetOffset() const { return OffsetPx; }
	float GetTargetOffset() const { return TargetOffsetPx; }

	void SetOffset(const float InOffset) { OffsetPx = InOffset; }
	void SetTargetOffset(const float InTarget) { TargetOffsetPx = InTarget; }

	/** Clamps both OffsetPx and TargetOffsetPx to [0, MaxOffset]. */
	void ClampOffset(float MaxOffset);

	/** Returns the current overscroll displacement in pixels. */
	float GetOverscrollOffset(const FGeometry& ViewportGeometry) const;

	// --- Physics ---

	/** Stops inertial scrolling and resets overscroll. */
	void ResetPhysics(bool bStopInertialScrollNow);

	/**
	 * Ticks the inertial scroll manager and overscroll spring.
	 * If pointer panning is inactive, applies any residual inertial velocity as a scroll delta.
	 *
	 * @param ViewportGeometry  The geometry of the clipping viewport (for overscroll calculation).
	 * @param DeltaTime         Frame delta time.
	 * @param MaxScrollOffset   Current maximum legal scroll offset (ContentHeight - ViewportHeight).
	 * @param OutScrollDeltaApplied  Set to true if inertial velocity produced a non-zero scroll delta this frame.
	 * @param OutOverscrollChanged   Set to true if the overscroll value changed visually this frame.
	 */
	void TickPhysics(
		const FGeometry& ViewportGeometry,
		float DeltaTime,
		float MaxScrollOffset,
		bool& OutScrollDeltaApplied,
		bool& OutOverscrollChanged);

	/**
	 * Applies a user-driven scroll delta (from touch drag, right-click pan, or inertial velocity).
	 * Handles overscroll rubber-banding at list boundaries.
	 *
	 * @param ViewportGeometry  The geometry of the clipping viewport.
	 * @param Delta             Pixel delta to scroll (positive = down).
	 * @param MaxScrollOffset   Current maximum legal scroll offset.
	 * @param bRecordInertialSample  When true, records the delta for inertial scroll velocity estimation.
	 */
	void ApplyScrollDelta(
		const FGeometry& ViewportGeometry,
		float Delta,
		float MaxScrollOffset,
		bool bRecordInertialSample);

	// --- Smooth scroll interpolation ---

	/**
	 * Interpolates OffsetPx toward TargetOffsetPx using FInterpTo.
	 * @return True if any interpolation was applied (i.e. offset changed).
	 */
	bool AdvanceSmoothScroll(float DeltaTime, float Speed, float MaxOffset);

	/** Snaps OffsetPx to TargetOffsetPx if smooth scroll is disabled and they differ. */
	bool SnapToTarget(float MaxOffset);

	// --- Pointer panning ---

	void BeginPan(const FVector2D& ScreenSpacePosition, bool bIsTouchGesture);
	void EndPan();

	bool IsPointerPanning() const { return bIsRightMousePanning || bIsTouchPanning; }
	bool IsRightMousePanning() const { return bIsRightMousePanning; }
	bool IsTouchPanning() const { return bIsTouchPanning; }

	FVector2D GetLastPointerPosition() const { return LastPointerScreenSpacePosition; }
	void SetLastPointerPosition(const FVector2D& InPosition) { LastPointerScreenSpacePosition = InPosition; }

	// --- Snap step tracking ---

	bool IsSnapStepInProgress() const { return bSnapStepInProgress; }
	float GetSnapStepTarget() const { return SnapStepTargetOffset; }

	void BeginSnapStep(float TargetOffset);
	void EndSnapStep();

	/**
	 * Checks whether the current offset has converged to the snap target.
	 * If so, snaps to the exact target and clears the snap step.
	 */
	bool CheckSnapStepConvergence();

	// --- Inertial scroll queries (used by parent for idle-state decisions) ---

	bool HasInertialVelocity() const;
	bool HasOverscroll(const FGeometry& ViewportGeometry) const;

private:
	// --- Scroll position ---
	float OffsetPx = 0.0f;
	float TargetOffsetPx = 0.0f;

	// --- Physics engines ---
	FInertialScrollManager InertialScrollManager{ 0.1 };
	FOverscroll Overscroll;

	// --- Pointer panning ---
	bool bIsRightMousePanning = false;
	bool bIsTouchPanning = false;
	FVector2D LastPointerScreenSpacePosition = FVector2D::ZeroVector;
	/** Timestamp of the last user-driven scroll delta during panning (for stale velocity clearing). */
	double LastPanScrollTime = 0.0;

	// --- Snap step ---
	bool bSnapStepInProgress = false;
	float SnapStepTargetOffset = 0.0f;
};