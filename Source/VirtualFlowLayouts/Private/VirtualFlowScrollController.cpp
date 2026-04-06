// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowScrollController.h"

// ---------------------------------------------------------------------------
// Offset management
// ---------------------------------------------------------------------------

void FVirtualFlowScrollController::ClampOffset(const float MaxOffset)
{
	OffsetPx = FMath::Clamp(OffsetPx, 0.0f, MaxOffset);
	TargetOffsetPx = FMath::Clamp(TargetOffsetPx, 0.0f, MaxOffset);
}

float FVirtualFlowScrollController::GetOverscrollOffset(const FGeometry& ViewportGeometry, const bool bIsHorizontal) const
{
	const float MainExtent = bIsHorizontal
		? ViewportGeometry.GetLocalSize().X
		: ViewportGeometry.GetLocalSize().Y;
	return MainExtent > 0.0f
		? Overscroll.GetOverscroll(ViewportGeometry)
		: 0.0f;
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

void FVirtualFlowScrollController::ResetPhysics(const bool bStopInertialScrollNow)
{
	InertialScrollManager.ClearScrollVelocity(bStopInertialScrollNow);
	Overscroll.ResetOverscroll();
}

void FVirtualFlowScrollController::TickPhysics(
	const FGeometry& ViewportGeometry,
	const float DeltaTime,
	const float MaxScrollOffset,
	bool& OutScrollDeltaApplied,
	bool& OutOverscrollChanged)
{
	OutScrollDeltaApplied = false;
	OutOverscrollChanged = false;

	const float PreviousOverscroll = Overscroll.GetOverscroll(ViewportGeometry);

	if (IsPointerPanning())
	{
		// While the user is actively dragging, clear inertia updates and give the user control over the scroll
		constexpr double PanIdleTimeout = 0.1;
		if (LastPanScrollTime > 0.0 && (FPlatformTime::Seconds() - LastPanScrollTime) > PanIdleTimeout)
		{
			InertialScrollManager.ClearScrollVelocity();
		}
		return;
	}

	// Process inertia before ticking the overscroll spring, matching SScrollBox::UpdateInertialScroll.
	InertialScrollManager.UpdateScrollVelocity(DeltaTime);
	const float Velocity = InertialScrollManager.GetScrollVelocity();

	if (!FMath::IsNearlyZero(Velocity, 0.1f))
	{
		// Mirror SScrollBox::CanUseInertialScroll: allow inertial velocity when there is no
		// overscroll, or when the velocity direction opposes the overscroll (scrolling back
		// inward). Only kill velocity that would push deeper into the overscroll region.
		const bool bCanUseInertialScroll =
			FMath::IsNearlyZero(PreviousOverscroll, KINDA_SMALL_NUMBER)
			|| FMath::Sign(PreviousOverscroll) != FMath::Sign(Velocity);

		if (bCanUseInertialScroll)
		{
			ApplyScrollDelta(ViewportGeometry, Velocity * DeltaTime, MaxScrollOffset, /*bRecordInertialSample=*/ false);
			OutScrollDeltaApplied = true;
		}
		else
		{
			InertialScrollManager.ClearScrollVelocity();
		}
	}

	// Tick the overscroll spring after inertia, so any opposing velocity has already helped
	// reduce the overscroll displacement before the spring applies its pull.
	Overscroll.UpdateOverscroll(DeltaTime);

	const float NewOverscroll = Overscroll.GetOverscroll(ViewportGeometry);
	if (!FMath::IsNearlyEqual(PreviousOverscroll, NewOverscroll, 0.1f))
	{
		OutOverscrollChanged = true;
	}
}

void FVirtualFlowScrollController::ApplyScrollDelta(
	const FGeometry& ViewportGeometry,
	const float Delta,
	const float MaxScrollOffset,
	const bool bRecordInertialSample)
{
	if (FMath::IsNearlyZero(Delta, KINDA_SMALL_NUMBER))
	{
		return;
	}

	const bool bIsAtStart = OffsetPx <= KINDA_SMALL_NUMBER;
	const bool bIsAtEnd = OffsetPx >= MaxScrollOffset - KINDA_SMALL_NUMBER;
	
	if (Overscroll.ShouldApplyOverscroll(bIsAtStart, bIsAtEnd, Delta))
	{
		Overscroll.ScrollBy(ViewportGeometry, Delta);
	}
	else
	{
		OffsetPx = FMath::Clamp(OffsetPx + Delta, 0.0f, MaxScrollOffset);
		TargetOffsetPx = OffsetPx;
	}

	// SScrollBox records inertial samples with the original screen-space delta before the
	// overscroll/offset split, so we do the same here to get correct velocity estimation.
	if (bRecordInertialSample && !FMath::IsNearlyZero(Delta, KINDA_SMALL_NUMBER))
	{
		const double Now = FPlatformTime::Seconds();
		LastPanScrollTime = Now;
		InertialScrollManager.ResetShouldStopScrollNow();
		InertialScrollManager.AddScrollSample(Delta, Now);
	}
}

// ---------------------------------------------------------------------------
// Smooth scroll interpolation
// ---------------------------------------------------------------------------

bool FVirtualFlowScrollController::AdvanceSmoothScroll(const float DeltaTime, const float Speed, const float MaxOffset)
{
	if (FMath::IsNearlyEqual(OffsetPx, TargetOffsetPx, 0.25f))
	{
		// Snap exactly to the target on convergence so the final paint pass
		// positions items at the precise target offset. Without this, a persistent
		// sub-pixel gap causes stale-geometry corrections to trigger extra scrolls.
		if (OffsetPx != TargetOffsetPx)
		{
			OffsetPx = TargetOffsetPx;
			ClampOffset(MaxOffset);
			return true; // One final frame at the exact target
		}
		return false;
	}

	OffsetPx = FMath::FInterpTo(OffsetPx, TargetOffsetPx, DeltaTime, Speed);
	ClampOffset(MaxOffset);
	return true;
}

bool FVirtualFlowScrollController::SnapToTarget(const float MaxOffset)
{
	if (FMath::IsNearlyEqual(OffsetPx, TargetOffsetPx, 0.1f))
	{
		return false;
	}

	OffsetPx = TargetOffsetPx;
	ClampOffset(MaxOffset);
	return true;
}

// ---------------------------------------------------------------------------
// Pointer panning
// ---------------------------------------------------------------------------

void FVirtualFlowScrollController::BeginPan(const FVector2D& ScreenSpacePosition, const bool bIsTouchGesture)
{
	LastPointerScreenSpacePosition = ScreenSpacePosition;
	bIsTouchPanning = bIsTouchGesture;
	bIsRightMousePanning = !bIsTouchGesture;
	LastPanScrollTime = 0.0;
	InertialScrollManager.ClearScrollVelocity(false);
	InertialScrollManager.ResetShouldStopScrollNow();
	TargetOffsetPx = OffsetPx;
}

void FVirtualFlowScrollController::EndPan()
{
	bIsRightMousePanning = false;
	bIsTouchPanning = false;
}

// ---------------------------------------------------------------------------
// Snap step tracking
// ---------------------------------------------------------------------------

void FVirtualFlowScrollController::BeginSnapStep(const float InTargetOffset)
{
	bSnapStepInProgress = true;
	SnapStepTargetOffset = InTargetOffset;
}

void FVirtualFlowScrollController::EndSnapStep()
{
	bSnapStepInProgress = false;
}

bool FVirtualFlowScrollController::CheckSnapStepConvergence()
{
	if (!bSnapStepInProgress)
	{
		return false;
	}

	if (FMath::IsNearlyEqual(OffsetPx, SnapStepTargetOffset, 1.0f))
	{
		OffsetPx = SnapStepTargetOffset;
		TargetOffsetPx = SnapStepTargetOffset;
		bSnapStepInProgress = false;
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Inertial scroll queries
// ---------------------------------------------------------------------------

bool FVirtualFlowScrollController::HasInertialVelocity() const
{
	return !FMath::IsNearlyZero(InertialScrollManager.GetScrollVelocity(), 0.1f);
}

bool FVirtualFlowScrollController::HasOverscroll(const FGeometry& ViewportGeometry, const bool bIsHorizontal) const
{
	return !FMath::IsNearlyZero(GetOverscrollOffset(ViewportGeometry, bIsHorizontal), 0.1f);
}