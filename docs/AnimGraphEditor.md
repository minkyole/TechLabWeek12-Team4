# AnimGraph 편집기 사용 방법

## 통합 편집 화면

Content Drawer에서 `.animgraph` 또는 `.animsm`을 더블클릭하면 **AnimGraph Editor**의 별도 문서 탭으로 열린다. 새 에셋은 빈 공간 우클릭 → Create → AnimGraph / State Machine으로 만든다.

- **상단:** Save, Compile, Play/Pause, Stop, Reset, Preview/PIE 상태와 문서 수정 표시.
- **왼쪽:** 독립된 3D Preview와 My Animation의 그래프·변수 탐색.
- **중앙:** 그래프 탭, 경로 탐색, 노드 캔버스.
- **오른쪽:** Details, Preview Parameters, StateMachine의 Observe 탭.
- **하단:** Compiler Results와 Asset Browser.

초기 가로 비율은 왼쪽 23%, 중앙 55%, 오른쪽 22%다. 창은 크기 조절·도킹·분리·닫기가 가능하다. Window 메뉴에서 패널을 다시 표시하거나 Reset Layout으로 기본 배치를 복원한다. 도킹 배치는 기존 ImGui 설정에, 패널 표시와 그래프 뷰 설정은 `Saved/AnimationEditor`에 보관한다. 에셋 파일에는 UI 레이아웃을 저장하지 않는다.

## 그래프 작성과 탐색

지원 노드는 Sequence Player, 1D BlendSpace Player, Two-way Blend, State Machine, Output Pose다. 상태 내부도 같은 Pose Graph 모델과 편집기를 사용한다. 전이 조건은 기존 Rule Graph를 사용한다.

1. 빈 캔버스를 우클릭하고 노드 이름을 검색한다. 위/아래 키와 Enter로 선택할 수 있다.
2. Pose 핀을 드래그하여 연결한다. 핀을 빈 공간으로 끌면 연결 가능한 노드 검색 메뉴가 열리고 생성 후 자동 연결된다.
3. Asset Browser에서 현재 메시와 호환되는 에셋을 검색하고 Pose 캔버스에 드롭하여 Player 또는 State Machine 노드를 만든다. State Machine은 등록된 `.animsm` 에셋을 표시한다.
4. 노드의 Alpha/Axis 상수는 노드 안에서 조절한다. 파라미터 바인딩·재생 옵션·에셋 참조는 Details에서 편집한다.
5. State Machine 노드를 더블클릭하면 원래 `.animsm` 파일이 같은 호스트의 별도 문서로 열린다. 참조 원본의 수정 표시와 저장 대상은 문서 탭에서 확인한다.
6. State Machine의 상태 더블클릭은 상태 내부 Pose Graph, 전이 더블클릭은 Transition Rule을 연다. My Animation, 그래프 탭, 상위 경로 버튼과 뒤로/앞으로 버튼으로 이동한다.

탐색은 재생을 재시작하지 않는다. 그래프별 확대·이동·선택은 각 노드 편집 세션이 유지한다. 참조 에셋은 현재 문서에 복사하지 않고 원래 파일을 편집한다.

### 디자인

Player는 녹색, Blend는 청록색, State Machine은 파란색, Output/Result는 붉은색 계열 헤더를 사용한다. 선택은 주황색 테두리, Pose 연결은 흰색 곡선, Bool 연결은 붉은색으로 표시한다. Float 변수는 녹색 `[F]`, Bool은 `[B]`, Trigger는 `[!]`로 구별한다. 전체 경로와 노드 ID는 툴팁·Details·검증 결과에서 확인한다. 실행 강조는 관찰 가능한 State Machine 상태·전이에만 적용한다.

### 조작

| 입력 | 동작 |
|---|---|
| 마우스 휠 / 중간 버튼 드래그 | 확대·축소 / 캔버스 이동 |
| F / Home | 선택 노드 / 전체 그래프 맞춤 |
| 우클릭 | 생성 검색 또는 선택 대상 메뉴 |
| 핀 우클릭 | 연결 해제 |
| Delete | 선택 노드·연결 삭제 |
| Ctrl+C / Ctrl+V / Ctrl+D | 복사 / 붙여넣기 / 복제 |
| Ctrl+Z / Ctrl+Y 또는 Ctrl+Shift+Z | 실행 취소 / 다시 실행 |
| Ctrl+S | 현재 에셋 저장 |

캔버스 단축키는 그래프에 포커스가 있고 텍스트 입력 중이 아닐 때 처리한다. 복사는 같은 그래프 종류끼리 지원하고 새 ID를 부여한다. Output/Result/Entry/AnyState 같은 필수 노드는 복제·삭제할 수 없다. 포즈 출력과 입력은 각각 연결 하나만 허용하며 순환 연결은 거부한다.

편집 이력은 에셋별 최근 100개 작업을 보관한다. 노드 드래그·연속 값 입력은 하나의 편집으로 묶고, 탐색·선택·수동 프리뷰 파라미터 변경은 제외한다. 저장되지 않은 문서를 닫으면 Save/Discard/Cancel을 선택한다.

## Compile, Save와 진단

**Compile**은 저장 전 작업 복사본을 검증하고 독립 프리뷰 런타임을 구성한다. 성공했을 때만 프리뷰를 교체하며 저장된 씬 컴포넌트는 변경하지 않는다. 실패하면 마지막 정상 프리뷰를 유지하고 Preview Parameters에 현재 수정본과 다르다는 안내를 표시한다.

**Save**는 현재 문서 파일을 저장한다. 기존과 같이 해당 에셋 및 의존 `.animsm`을 사용하는 컴포넌트를 재구성한다. 외부 파일 변경 자동 감시는 포함하지 않는다.

상태는 Needs Compile / Up to date / Compile Error로 표시한다. Compiler Results의 오류·경고를 클릭하면 해당 그래프와 식별 가능한 노드로 이동한다. 기존 문자열 오류에 노드 위치가 없는 경우에는 해당 그래프까지 이동한다. 미연결 노드는 경고로 표시하고 실행에서 제외한다.

## 변수와 프리뷰

My Animation의 Variables에서 변수를 선택하면 Details에 이름·타입·기본값이 나타난다. 이름 변경 시 현재 에셋 내부의 Pose 바인딩·전이 조건·소비 Trigger 참조도 갱신한다. 사용 중인 변수의 삭제·타입 변경은 사용 위치와 확인 창을 표시한다. 확인 후 남은 불일치는 Compile로 확인하고 수정한다. Lua 스크립트와 외부 참조 에셋의 이름은 자동 수정하지 않는다.

내장 `.animsm`의 파라미터는 상위 저장소에 이름·타입으로 연결된다. 동일 이름의 타입 충돌은 오류다. 초기값은 최상위에 명시하는 것을 권장한다. Trigger 소비도 같은 저장소에 반영된다.

프리뷰는 Mesh Viewer와 렌더러를 공유하지만 컴포넌트·카메라·렌더 타깃·재생 상태는 독립적이다. Preview Parameters에서 Bool·Float·Trigger를 조작한다. 기본 프리뷰는 Lua UpdateScript를 실행하지 않는다. Play/Pause는 진행을 제어하고 Stop은 처음으로 돌려 정지하며 Reset은 현재 작업본을 다시 Compile한다. 그래프 탐색은 프리뷰 상태를 유지한다.

StateMachine 문서의 Observe 탭에서 기존 PIE 대상을 선택할 수 있다. PIE 관찰은 읽기 전용이고 실제 상태·전환 정보만 강조한다. AnimGraph용 새 PIE 디버거는 제공하지 않는다.

## 런타임과 호환성

`UAnimGraphAsset`(`.animgraph`)은 공유 정의이고 `UAnimGraphInstance`는 컴포넌트별 실행 상태를 소유한다. 기존 `.animsm`은 로드 시 상태를 `Player → Output Pose`로 변환하며, 저장 시 Version 2로 기록한다. 로드만으로 원본은 변경하지 않는다. 이번 UI 개편은 에셋 저장 형식이나 Animation Mode의 의미를 바꾸지 않는다.

| Animation Mode | 실행체 | 설정 |
|---|---|---|
| Lua | ULuaAnimInstance | AnimationScriptPath |
| StateMachine | UAssetAnimInstance | StateMachineAssetPath |
| AnimGraph | UAnimGraphInstance | AnimGraphAssetPath |
| SingleNode | UAnimSingleNodeInstance | 기존 단일 재생 API |

런타임 AnimGraph는 선택적 UpdateScript의 `UpdateAnimation(anim, dt)`를 실행한 후 파라미터 바인딩과 포즈를 갱신한다. `SetBool`, `SetFloat`, `SetTrigger`, `ResetTrigger`, `GetBool`, `GetFloat`, `IsTriggerActive`, `GetMovementComponent`를 사용한다. 캐릭터 Lua의 `actor:GetAnimInstance()`도 현재 인스턴스를 반환한다. AnimGraph 안에서는 최상위 스크립트만 실행하고, 독립 StateMachine 모드에서는 `.animsm` 스크립트를 실행한다.

### Lua 모드의 상태 머신 정의

`CharacterAnim.lua`는 `Animation Mode = Lua`에서 `ULuaAnimInstance`가 호출하는 초기화 스크립트다. 현재 `NewStateMachine.animsm`의 9개 상태와 16개 전이를 Lua로 구성하며, `.animsm` 파일 자체를 로드하지 않는다. 이후 에셋을 수정하면 이 스크립트의 정의도 별도로 동기화해야 한다. Scene의 모드는 자동 변경하지 않는다.

Lua 생성 API에 `anim:CreateTwoWayBlend(aNodeId, bNodeId, initialAlpha)`와 `anim:SetBlendAlpha(blendNodeId, alpha)`를 추가했다. 기존 Player 노드를 A/B에 연결한 뒤 Blend 노드 ID를 `AddState`에 전달한다. 입력 노드는 공유할 수 없으며 Alpha는 0~1로 제한한다. CharacterAnim은 일반/검 이동 BlendSpace를 만들고, UpdateAnimation에서 Speed와 AttackReady를 입력에 반영한다. 키 입력·Trigger 요청·AttackReady 조절은 기존 AnimationTest.lua가 담당한다.

범용 Blueprint 실행, 새 연산 노드·데이터 핀, Cached Pose, 포즈 출력 공유, 2D BlendSpace, 리타게팅은 이번 범위에 포함하지 않는다.

수동 검증은 [AnimGraphValidationChecklist.md](AnimGraphValidationChecklist.md)를 따른다. UI 실행과 게임 실행 테스트는 사용자가 수행한다.
