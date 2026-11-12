# State Machine Editor

## Create and connect states

1. In Content Drawer, right-click an Assets folder and choose **Create > Animation > State Machine**. Select the skeletal mesh and an `.animsm` save path under Assets.
2. Right-click the graph background and choose **Add State**. Compact boxes show the state name and Sequence/BlendSpace type. Select an animation and edit playback options in Details. Declare Bool, Float and Trigger parameters under Parameters; bind each BlendSpace axis to a Float parameter.
3. Right-click a state and choose **Make Transition**, then click the target state. The source and valid targets are highlighted, with a temporary arrow following the cursor. Select the source itself to create a self transition. Escape or a background click cancels. Closing the asset or switching to the condition graph also cancels.
4. Use the same action on **Entry** to set or replace the initial state, or on **AnyState** to create an any-state transition. Entry and AnyState cannot be deleted.
5. A transition is one directional link from state border to state border, with a small diamond in the middle. Click anywhere on the line or diamond to select it. Hover for its endpoints and condition summary; Details contains the full rule summary and transition options.
6. Double-click the line or diamond, or choose **Edit Transition Rule**, to open its condition graph. Click **State Machine** in the breadcrumb to return.

State graphs have no visible In/Out pins or separate transition nodes. Opposite and parallel transitions use separate lanes. Self transitions loop outside the state. Left-drag moves boxes; middle-drag pans; the wheel zooms. Frame All and Frame Selection focus the canvas. Delete and multi-selection deletion remain available. Removing a state removes its transitions; deleting Entry's link clears the initial-state choice. Ctrl+S saves.

**Auto Arrange** explicitly rearranges state boxes into deterministic BFS layers rooted at Entry (or the lowest state ID if Entry is unset). Return transitions participate as undirected neighbors so cycles do not break layout. Disconnected components get separate vertical regions. Entry is placed left of the initial state and AnyState above the graph. The operation marks the asset modified and frames all content; save to retain positions. Existing layouts are preserved until the button is pressed. Routing does not search for detours around unrelated state boxes; automatic arrangement and manual movement can reduce remaining crossings.

## Edit a transition rule

Conditions are named nodes, not indices. Connect Bool outputs to inputs and finish at the protected **Can Enter Transition** node. For example:

```
[IsGrounded == true] -------\
                            AND --> [Can Enter Transition]
[AttackRequested active] ---/
```

Right-click empty space or drag a pin into empty space to add a compatible node. Supported nodes are Bool Constant, Bool Compare, Float Compare, Trigger Active, Source Finished, State Elapsed, AND, OR and NOT. Comparisons select a declared parameter from a type-filtered dropdown and compare it to the value inside the node. Separate Float value nodes and comparisons between two parameters are not included.

AND/OR have **+ Input** and **- Input** controls. NOT and the final output take one input. Connecting to an occupied input replaces its old connection; an output can feed several inputs. Cyclic rule links are rejected. Cyclic state transitions and self transitions are allowed.

New transitions start with **false -> Can Enter Transition**, so they cannot fire until configured. Missing required connections and invalid parameters are highlighted on a node and listed in Rule Errors. Click an error to open its rule. Invalid graphs block save and preview application. Unused nodes retain their layout but do not produce runtime conditions. Unused predicate nodes must still have valid parameter/value settings.

Parameter renames update rule references, BlendSpace bindings and consumed triggers inside the asset. Removing or changing the type of a referenced parameter is blocked with a list of its users. Lua script strings must be renamed manually.

## Preview and apply

**Apply to 3D Preview** opens Skeletal Mesh Viewer with a private snapshot of the working data, including unsaved edits. Play/Pause, Reset, Bool/Float edits and Trigger buttons control this instance. The state graph highlights the current state and the exact active transition. Apply again after editing. Lua updates are disabled in preview.

For a character, set its skeletal mesh component's **StateMachineAssetPath** to the `.animsm` file, for example `Assets/NewStateMachine.animsm`. The mesh must match. The path persists with scene serialization and duplication. Clear it to return to the default Lua instance.

C++ can use `GetStateMachineAnimInstance()` or cast the component's animation instance to `UStateMachineAnimInstance`. Actor Lua scripts can use `self:GetAnimInstance()` and `SetBool`, `SetFloat`, `SetTrigger` or `ResetTrigger`. An optional asset update script must provide `UpdateAnimation(anim, dt)`; see `Assets/Scripts/Animation/StateMachineUpdate.lua`.

## PIE observation and docking

The State Machine Editor is an independent asset window: drag its tab/title bar to undock or dock it in the root workspace, like Blend Space Editor. Old layouts that grouped it with the locked level viewport are repaired for this window only; other panel layouts and viewport locking are retained.

The **Observe** selector switches between **Preview** and **PIE**. Starting a new PIE session selects PIE observation. The **PIE Target** dropdown lists actor / skeletal mesh component instances using the open asset; a single candidate is selected automatically. With several candidates, choose the actor/component explicitly. Choosing Preview during PIE keeps that choice for the rest of the session. Stopping PIE clears the target and returns to Preview.

The panel displays the actual current state and transition progress and highlights that state and the exact active link. PIE observation is read-only: preview parameter controls and Reset are unavailable in PIE observation mode. Engine pause and frame stepping retain/update the displayed runtime snapshot normally. Actor deletion, animation instance replacement and PIE restart are resolved through fresh observations, not retained runtime pointers.

If the edited state/transition/condition definition differs from the running snapshot, highlighting stops with an explanation. Moving nodes does not invalidate observation. Reapply the preview or restart PIE after behavior changes; this feature does not rebuild running instances. An uninitialized graph or missing candidate is reported without retaining an old highlight.

## Compatibility and limits

Existing Version 1 `.animsm` files open without manual migration. Their conditions are converted into independent per-transition graphs in memory. Editing one transition no longer changes other transitions that previously shared its condition. Opening a file does not rewrite it; saving adds optional editor metadata and regenerates the existing runtime condition arrays. State IDs, state positions, transition order and behavior are preserved. Legacy transition badge coordinates are retained in the file for compatibility but are not used by the direct-link view. No `NodeEditor.json` sidecar is created.

Each state still contains one Sequence or one nonempty 1D BlendSpace. Undo/Redo, copy/paste, a free-form AnimGraph, nested state machines, general Blueprint operations and live rebuilding of running characters are outside this version. Save does not change existing runtime snapshots; reapply or restart play to use edits.

Implementation verification covers Debug x64 compilation and linking only. The application and gameplay behavior are not run automatically.
