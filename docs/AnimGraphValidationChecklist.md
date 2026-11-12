# AnimGraph 수동 검증 목록

실행 테스트는 사용자에게 맡긴다. 아래 항목은 **미실행 상태**이며 완료 표시가 검증 결과를 의미한다.

빌드 확인: 2026-11-11, Debug x64의 `HitoriEngine.lib`와 `HitoriEditor.exe` 생성 성공(오류 0). Visual Studio PDB 관리자 불일치(C1902)를 우회하기 위해 해당 빌드 프로세스에만 `_CL_=/Z7`을 적용했다. 프로젝트의 영구 컴파일 옵션은 변경하지 않았다. 로그: `Build/AnimGraphVerifiedBuild.log`. 게임·에디터 실행 및 테스트 실행 파일은 실행하지 않았다. 신규 소스는 기존 `premake5.lua`의 Runtime/Editor 재귀 소스 수집에 포함된다.

## 준비

- [ ] 동일 Skeletal Mesh의 서로 다른 Sequence A/B, 루프 이동 클립, 비루프 공격 클립을 준비한다.
- [ ] 같은 메시의 1D BlendSpace와 기존 `.animsm`을 준비한다. 원본 `.animsm` 및 씬은 복사본을 사용한다.
- [ ] Notify/NotifyState가 있는 클립과 RootMotion이 있는 클립을 준비하고 로그·캐릭터 이동을 관찰할 수 있게 한다.

## 최상위 그래프

- [ ] Content Drawer에서 AnimGraph를 생성하고 `Sequence → Output Pose`를 연결한다. 저장 전 Apply to 3D Preview에서 재생된다.
- [ ] `Sequence A/B → Two-way Blend → Output Pose`를 구성한다. Alpha 0은 A, 1은 B, 0.5는 두 포즈의 혼합이다.
- [ ] Alpha를 범위 밖(-1, 2)으로 입력하면 양 끝 포즈로 제한된다.
- [ ] Float 파라미터를 Alpha에 바인딩하고 프리뷰 입력을 바꿀 때 포즈가 변한다.
- [ ] BlendSpace를 연결하고 Axis 상수·Float 바인딩으로 샘플 혼합이 변한다.
- [ ] Loop, Playing, Play rate 0·양수·음수, Play/Pause, Reset 동작을 확인한다.
- [ ] Alpha가 0/1일 때 가중치가 없는 가지의 시간이 진행하지 않고, 다시 가중치를 주면 이어서 진행한다.
- [ ] 노드 이동, 연결 삭제, 노드 삭제, Frame all, Ctrl+S, 재열기 후 노드 위치와 설정이 보존된다.
- [ ] 수정한 그래프를 닫거나 다른 에셋을 열 때 Save/Discard/Cancel이 의도대로 동작한다.

## State Machine과 상태 내부

- [ ] `State Machine(.animsm) + Sequence → Blend → Output`을 구성하고 두 입력이 혼합된다.
- [ ] State Machine Editor에서 상태를 더블클릭하여 `Sequence A/B → Blend → Output`을 작성한다. 상위 탐색 버튼으로 돌아갈 수 있다.
- [ ] 전이선을 더블클릭하면 기존 Rule Graph가 열리고 조건·우선순위·Blend duration이 유지된다.
- [ ] Bool·Float·Trigger 전이와 SourceFinished 조건이 상태 내부 포즈 그래프에서 동작한다.
- [ ] 비루프 A/B 혼합의 SourceFinished는 유효 가중치 입력이 모두 완료된 뒤 참이다. 루프 입력에 유효 가중치가 있으면 완료되지 않는다.
- [ ] 재진입·동일 상태 재시작·AnyState·전환 도중 인터럽트를 확인한다. 재진입 시 하위 Player 시간이 초기화된다.
- [ ] 비활성 상태에 중첩 State Machine을 두어도 초기화 시 assert가 없고 해당 상태 진입 전에는 진행하지 않는다.
- [ ] 중첩 State Machine의 파라미터가 이름·타입으로 연결되고 Trigger 소비가 상위에서도 관찰된다.
- [ ] State Machine 상태 내부에 다른 `.animsm`을 참조한 구조를 저장·재로드해 재생한다.

## Animation Mode와 씬

- [ ] Lua / StateMachine / AnimGraph / SingleNode 선택 시 실제 실행체가 바뀌고 해당 모드의 경로만 표시된다.
- [ ] 모드를 반복 전환해도 각 경로가 보존되며, 경로 입력만으로 모드가 바뀌지 않는다.
- [ ] 활성 에셋·스크립트·메시를 변경하면 새 설정으로 초기화되고 이전 NotifyState가 종료된다.
- [ ] 서로 다른 모드의 컴포넌트를 씬에 저장하고 재로드한 뒤 모드와 경로가 유지된다.
- [ ] Actor/컴포넌트를 복제하면 설정이 같고, 실행 시간·파라미터·현재 상태는 서로 독립적이다.
- [ ] 구버전 씬은 `.animsm` 경로 유무에 따라 StateMachine/Lua로 복원된다.
- [ ] 기존 캐릭터 Lua, 기존 `.animsm`, Sequence/BlendSpace 뷰어의 SingleNode 프리뷰가 계속 동작한다.
- [ ] 편집기에서 그래프 또는 의존 `.animsm`을 저장하면 관련 컴포넌트만 재구성되며 재생 상태가 초기화된다.

## Lua·Notify·RootMotion

- [ ] AnimGraph UpdateScript가 프레임마다 파라미터를 갱신하고 그 프레임 포즈에 반영된다.
- [ ] 캐릭터 Lua의 `GetAnimInstance()`로 AnimGraph를 얻어 Bool/Float/Trigger를 설정·조회할 수 있다.
- [ ] 독립 StateMachine에서는 `.animsm` UpdateScript가 실행되고, AnimGraph 안에서는 최상위 스크립트만 실행된다.
- [ ] 스크립트 오류는 로그에 표시되고 동일 오류가 매 프레임 반복 실행되지 않는다.
- [ ] 가중치 0 가지에서 Notify와 RootMotion이 발생하지 않고, 혼합 중에는 가중치에 맞게 RootMotion이 반영된다.
- [ ] 루프·역재생·전환·인터럽트에서 Notify의 중복 발생이나 누락을 확인한다.
- [ ] NotifyState 도중 모드 변경, 에셋 변경, 메시 변경, 재빌드, 컴포넌트 삭제 시 End가 한 번 발생한다.
- [ ] 같은 에셋을 사용하는 두 캐릭터의 Notify·RootMotion·Trigger가 서로 섞이지 않는다.

## 오류와 저장 호환성

- [ ] 순환 연결, 같은 출력의 다중 연결, 같은 입력의 중복 연결을 편집기가 거부한다.
- [ ] 출력 필수 입력 누락, 잘못된 에셋 키, 메시 불일치, 비어 있는 BlendSpace를 저장·프리뷰에서 오류로 안내한다.
- [ ] 이름 중복 파라미터, Float 바인딩 타입 불일치, 내장 `.animsm`과의 타입 충돌을 오류로 안내한다.
- [ ] 직접/간접 `.animsm` 순환 참조를 거부하며 크래시·무한 재귀가 없다.
- [ ] 연결되지 않은 미완성 노드를 저장할 수 있고 실행 결과에는 영향을 주지 않는다.
- [ ] 파일 누락·잘못된 JSON·지원하지 않는 버전에서 선택 모드가 유지되고 참조 포즈와 오류를 제공한다.
- [ ] 구버전 `.animsm`을 열기만 하면 파일이 바뀌지 않는다. 저장 후 Version 2 및 PoseGraph가 기록되고 재열기 후 동일하게 동작한다.

## 검증 기록

| 항목 | 결과 | 재현 에셋/씬 | 메모 |
|---|---|---|---|
| 최상위 그래프 | 미실행 | | |
| 상태 내부/중첩 | 미실행 | | |
| 모드/저장/복제 | 미실행 | | |
| Lua | 미실행 | | |
| Notify/RootMotion | 미실행 | | |
| 오류/호환성 | 미실행 | | |

## CharacterAnim.lua의 Lua 모드 정의

아래 항목은 **미실행**이다. `CharacterAnim.lua`는 `.animsm`의 UpdateScript용이 아니라 `Animation Mode = Lua`에서 그래프를 직접 구성하는 초기화 스크립트다.

- [ ] 메시를 `UAL1_Standard_RM`으로 지정하고 Animation Mode를 Lua, AnimationScriptPath를 `Assets/Scripts/Animation/CharacterAnim.lua`로 설정한다. 초기화 오류 없이 Locomotion으로 시작한다.
- [ ] `Speed`가 일반/검 이동 BlendSpace 양쪽에 적용되고, `AttackReady` 0/0.5/1에서 두 이동 포즈가 혼합된다. 입력과 AttackReady 갱신은 기존 AnimationTest.lua를 사용한다.
- [ ] 좌클릭 Attack, H Hit, Y Death, R Roll 요청이 동작한다. Death 종료 후 Revival(Sitting_Exit)을 거쳐 Locomotion으로 돌아온다.
- [ ] JumpStart/JumpLoop/JumpLand와 Roll 도중 공중 진입 전이가 저장된 `.animsm`과 일치한다. JumpLand는 클립 완료 또는 경과 시간 > 0.2초이면서 Speed > 0.1일 때 복귀한다.
- [ ] 공격 진입의 SmoothStep, 각 전환의 시간·우선순위·인터럽트·Trigger 소비 및 Roll 재생 속도 1.4를 확인한다.
- [ ] Lua 모드 컴포넌트 두 개의 재생 시간과 AttackReady가 독립적이고, 모드 전환·인스턴스 재초기화 후에도 정상 구성된다.

## 통합 애니메이션 편집기 UI/UX 개편

아래 항목도 모두 **미실행**이다. Runtime·Editor 빌드만 확인하고 편집기/게임은 실행하지 않는다.

UI 개편 빌드 확인: 2026-11-11, Debug x64 Runtime·Editor 빌드 성공(오류 0). 빌드 프로세스에만 `_CL_=/Z7` 적용. 로그: `Build/AnimationEditorUXFinalBuild.log`. `git diff --check` 통과. 실제 화면·입력·렌더링 동작은 아래 항목으로 확인한다.

### 레이아웃과 디자인

- [ ] `.animgraph`와 `.animsm`을 열면 같은 AnimGraph Editor 호스트의 문서 탭에 표시된다. 기존 Window 메뉴의 State Machine Editor 진입도 공통 호스트를 연다.
- [ ] 기본 배치가 왼쪽 Preview/My Animation, 중앙 Graph, 오른쪽 Details, 하단 Compiler Results/Asset Browser다. 가로 비율이 약 23:55:22다.
- [ ] 패널 크기 조절·도킹·분리·닫기 및 Window 메뉴 재표시가 동작한다. 편집기 재시작 후 배치와 표시 설정이 복원된다.
- [ ] Window → Reset Layout으로 모든 패널과 기본 배치가 복원된다. Level Editor·Mesh Viewer의 배치는 바뀌지 않는다.
- [ ] AnimGraph Editor가 Viewports 옆에 잠긴 기존 레이아웃에서 다시 열면 Level Editor와 같은 최상위 탭 그룹으로 이동한다. 이후 탭을 분리·다시 도킹할 수 있고 전체 레이아웃 초기화가 필요하지 않다.
- [ ] Player/Blend/StateMachine/Output 헤더 색상, 주황색 선택 테두리, 흰색 Pose 연결, 붉은색 Bool 연결이 식별된다.
- [ ] Float/Bool/Trigger 변수의 색상·문자 기호가 구별된다. 긴 에셋 경로는 노드를 과도하게 늘리지 않고 툴팁·Details에서 확인된다.
- [ ] State Machine/Player 에셋 이름 및 Rule 노드 제목에 마우스를 올리면 툴팁이 커서 근처에 표시된다. 그래프 패닝·줌·창 이동 후에도 화면 왼쪽 위로 튀지 않는다.
- [ ] State Machine의 활성 상태·전환 표시가 실제 관찰 값과 일치하고, 관찰할 수 없는 Pose 흐름을 활성으로 표시하지 않는다.
- [ ] 연결선 흐름 효과는 실제 트랜지션 중에만 재생되고, 종료·인터럽트·관찰 대상 해제 시 즉시 사라진다. 짧은/긴 전환과 Blend duration 0을 각각 확인한다.

### 생성·편집·이력

- [ ] 빈 Pose 캔버스 우클릭 검색에서 부분 검색, 위/아래 선택, Enter·Esc가 동작한다. Rule 검색에서도 동일하게 동작한다.
- [ ] 핀을 빈 공간에 드래그하면 연결 가능한 노드만 표시하고, 생성 후 올바른 입력에 자동 연결한다.
- [ ] Asset Browser의 메시 필터·검색이 동작하고, Sequence/BlendSpace/StateMachine 드롭 시 에셋이 지정된 노드를 만든다.
- [ ] Alpha/Axis를 노드 안에서 편집할 수 있고 Details의 값과 일치한다. 입력 핀과 출력 핀의 위치·연결 방향이 명확하다.
- [ ] 핀 우클릭 연결 해제, 노드 복제·삭제·참조 에셋 열기가 동작한다.
- [ ] 휠·MMB·F·Home·Delete 및 Ctrl+C/V/D가 Pose/State/Rule에서 동작한다. 필수 노드는 복제·삭제되지 않는다.
- [ ] 그래프 종류가 다른 클립보드는 섞이지 않고, 붙여넣은 노드·상태·전이는 새 ID를 가지며 선택 상태가 된다.
- [ ] Ctrl+Z/Y 및 Edit 메뉴 Undo/Redo가 에셋별로 동작한다. 다른 에셋의 이력을 변경하지 않는다.
- [ ] 노드 드래그와 연속 값 입력은 한 번의 Undo로 취소된다. Undo 후 새 편집 시 Redo 이력이 사라진다. 최근 100개 편집으로 제한된다.
- [ ] 변수·노드 이름 입력 중에는 캔버스 Delete/F/Home/Ctrl+Z가 잘못 실행되지 않는다. Preview Parameters 변경은 에셋 Dirty/Undo에 포함되지 않는다.

### 문서·탐색·변수

- [ ] 참조 State Machine 더블클릭 시 원래 `.animsm` 문서가 열리고 파일 이름·수정 표시·저장 대상이 구별된다.
- [ ] 상태 Pose와 전이 Rule을 탭·My Animation·경로·뒤로/앞으로로 오갈 수 있다. 그래프별 줌·위치·선택을 유지한다.
- [ ] 그래프 탐색만으로 프리뷰 재생이 초기화되지 않는다. 문서별 카메라·재생·수동 파라미터가 서로 독립적이다.
- [ ] 저장된 문서를 다시 열거나 편집기를 재시작했을 때 그래프 뷰 설정이 복원된다. 새 문서를 처음 저장한 경우도 확인한다.
- [ ] 수정된 문서 탭 또는 호스트를 닫을 때 Save/Discard/Cancel이 동작한다. Cancel 후 문서와 프리뷰가 유지된다.
- [ ] 변수 선택 시 Details에 이름·타입·기본값이 표시되고, 선택이 없으면 메시·UpdateScript 에셋 설정이 보인다.
- [ ] 변수 이름 변경 시 현재 에셋의 Pose·Rule·소비 Trigger 참조가 함께 바뀐다. 외부 참조 파일·Lua 스크립트는 자동 변경하지 않는다.
- [ ] 사용 중 변수의 삭제·타입 변경은 사용 위치와 확인 창을 표시한다. 취소하면 값이 유지되고, 계속하면 남은 오류를 Compile로 확인할 수 있다.

### Compile·프리뷰·호환성

- [ ] Compile은 저장되지 않은 수정본을 검증하고 프리뷰에 적용한다. 파일·씬 컴포넌트는 변경하지 않는다.
- [ ] Needs Compile/Up to date/Compile Error 상태가 편집과 컴파일 결과에 맞게 바뀐다.
- [ ] 정상 Compile 후 오류를 만들고 Compile하면 이전 정상 프리뷰가 유지되며 현재 수정본과 다르다는 안내가 나타난다.
- [ ] Compiler Results에서 오류·경고를 선택하면 해당 State/Pose/Rule과 식별 가능한 노드를 찾아 강조한다. 위치 없는 기존 오류는 해당 그래프를 연다.
- [ ] Preview의 Play/Pause/Stop/Reset과 Reset Camera가 동작한다. 기본 Lua 업데이트는 비활성이고 Preview Parameters로 직접 조작할 수 있다.
- [ ] Mesh Viewer에서 다른 메시·애니메이션을 열거나 카메라를 움직여도 애니메이션 편집기 프리뷰가 변하지 않는다. 반대 방향도 확인한다.
- [ ] 프리뷰에 포커스를 두고 Ctrl+S를 눌렀을 때 메시 파일을 저장하지 않고 현재 애니메이션 문서만 저장한다.
- [ ] StateMachine Observe 탭의 PIE 대상 선택·읽기 전용 관찰·상태/전환 강조가 유지된다. AnimGraph에 가짜 PIE 관찰 값을 표시하지 않는다.
- [ ] 기존 `.animgraph`/`.animsm` 저장·로드, 컴포넌트 Animation Mode, 저장 시 의존 컴포넌트 재빌드가 유지된다.
- [ ] 편집기 종료 시 프리뷰 컴포넌트와 렌더 타깃이 해제되고, 여러 문서 Compile/닫기를 반복해도 수명 오류가 없다.
