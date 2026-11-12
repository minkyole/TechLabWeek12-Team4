#pragma once

struct FAnimNode_Base;
struct FPoseContext;
struct FAnimationUpdateContext;

struct FPoseLink
{
	FAnimNode_Base* LinkedNode = nullptr;

	void OnEnter();
	void Update(const FAnimationUpdateContext& Context);
	void Evaluate(FPoseContext& Output);
};
