# BlendSpace 1D 편집기

## 사용

1. Content Drawer의 빈 영역을 우클릭하여 **Create > Animation > BlendSpace 1D**를 선택한다.
2. 이름과 기준 Skeletal Mesh를 지정한다. 기본 축은 Speed, 범위는 0~600이다.
3. Asset Browser의 애니메이션을 Blend Graph에 드래그한다. `+` 버튼은 축 중앙에 추가한다.
4. 마름모를 드래그하거나 Sample Details의 X 값을 입력해 위치를 바꾼다. 같은 위치에 두 샘플을 둘 수 없다.
5. 그래프를 선택한 상태에서 Delete 또는 샘플 우클릭 메뉴로 삭제한다. 드래그 중 Escape는 이동을 취소한다.
6. Save 또는 편집기에 포커스가 있을 때 Ctrl+S로 저장한다. Content Drawer에서 파일을 더블클릭하여 다시 연다.

좌우 패널 폭은 구분선을 드래그하여 조절한다. Sample Details와 Asset Browser 사이의 수평 구분선으로 높이를 조절한다. 미저장 상태로 닫거나 다른 에셋을 열면 Save / Discard / Cancel을 선택한다. 편집 중에는 작업 사본만 바뀌며 저장 성공 후 등록 객체에 반영된다.

이번 구현에는 3D 미리보기, 2D, Undo/Redo, Save As와 추가 보간 설정이 없다.

## 파일과 참조

`.blendspace`는 버전 1 JSON이다. 필드는 `Version`, `Type`, `SkeletalMeshAssetKey`, `Axis` (`Name`, `Min`, `Max`), `Samples` (`AnimationAssetKey`, `X`)이다. 빈 샘플 목록도 저장할 수 있다.

애니메이션은 FBX 임포트와 베이크 로드에서 `FBX 에셋 키#Anim/클립 이름`으로 등록된다. 예: `Models/UAL1_Standard.fbx#Anim/Armature|Idle_Loop`. 클립 이름의 `/`와 `\`는 `_`로 치환되고, 동일 FBX의 이름 중복은 인덱스 접미사로 구분된다. BlendSpace는 UAnimSequence의 GetPath()를 저장하고 AssetManager 조회로 복원한다. FBX 내부 참조를 직접 해석하지 않는다. 현재 같은 Skeletal Mesh를 사용하는 클립만 허용한다. 클립 이름이 변경되면 저장된 참조도 갱신해야 한다. 이전 숫자 기반 참조는 자동 변환하지 않으며 실제 클립 이름을 확인해 다시 지정해야 한다.

시작 시 원본 에셋과 애니메이션을 먼저 등록한 뒤 BlendSpace를 로드한다. 없는 참조, 잘못된 타입·버전·범위·중복 샘플은 파일 전체의 로드 실패로 처리한다. 저장은 같은 폴더의 임시 파일 작성 후 교체하며 실패 시 이전 파일을 보존한다.

## 빌드와 테스트

프로젝트 파일을 다시 생성할 때 premake의 기존 Runtime/Editor 소스 glob이 새 파일을 포함한다. Debug Runtime·Editor 빌드 후 **x64 Visual Studio Developer Command Prompt**에서 다음을 실행한다.

```bat
Tools\Tests\RunBlendSpaceTests.cmd
```

- `BlendSpaceAssetTests.cpp`: 47개 검사. 등록 참조, 저장·복원, 가중치, 손상 파일, 호환성, 저장 실패 보존, 단계별 스캔을 확인한다.
- `BlendSpaceEditorTests.cpp`: 15개 검사. 실제 ImGui 프레임에서 그래프 표시, 선택, 이동, Ctrl+S, 작업 사본, 삭제, 닫기·버리기와 재열기를 확인한다. GPU와 OS 창은 필요하지 않다.

테스트는 Assets 아래에 프로세스별 임시 폴더를 만들고 성공 후 제거한다. 실패한 테스트의 `_BlendSpaceTests_숫자` 또는 `_BlendSpaceUITests_숫자` 폴더는 실패 조사 후 제거할 수 있다.

검증 환경의 C1902 디버그 정보 도구 충돌은 로컬 빌드 옵션에서 `/Z7`과 PCH 비활성화를 사용해 우회했다. 저장소의 공통 빌드 설정은 변경하지 않았다.

## Skeletal Mesh Viewer에서 Blend Space 미리보기

1. Content Drawer에서 기준 Skeletal Mesh의 FBX를 열고 **Animation 옆의 Blend Space 탭**을 선택한다. 닫힌 탭은 Viewer의 **Window > Blend Space**에서 다시 연다.
2. 현재 메시와 호환되는 저장된 BlendSpace 1D를 선택한다. 새로 선택한 에셋은 축 최솟값·진행률 0에서 반복 재생한다.
3. 그래프에서 **Ctrl을 누른 채 마우스를 움직여** 입력값을 바꾼다. Ctrl을 놓으면 값을 유지한다. 슬라이더와 숫자 입력도 사용할 수 있다.
4. 초록색 십자는 현재 입력을 나타낸다. 그래프 아래에서 기여 샘플의 AssetKey와 가중치를 확인한다. **Ctrl+Alt**는 그래프에서 기여 샘플과 가중치를 강조한다.
5. 처음으로, 재생·일시정지, Loop, Speed, Progress로 재생을 제어한다. Progress는 모든 샘플에 공통인 정규화 진행률이며 초 단위 길이는 현재 입력의 가중치에 따라 바뀐다. 일시정지 중에도 입력을 바꾸면 포즈를 갱신한다.
6. 해당 탭 또는 공유 Viewport에 포커스가 있을 때 **Space**로 재생을 토글한다. 텍스트 입력, 팝업, 카메라 조작 중에는 적용하지 않는다.
7. 샘플 수정은 **Edit Asset**으로 기존 Blend Space Editor를 열어 수행한다. 저장한 수정은 동일 등록 객체를 통해 Viewer에 반영된다.

Sequence와 BlendSpace는 같은 Skeletal Mesh Viewport를 사용하며 각 모드의 재생 위치·재생 여부·속도·반복 설정을 별도로 보관한다. None을 선택하면 기준 포즈로 돌아간다. 메시를 바꾸면 이전 미리보기 선택을 해제한다. 미리보기 에셋이 선택된 동안에는 본 변형과 메시 저장을 차단한다.

Viewer가 닫히거나 Viewport가 표시되지 않으면 시간 진행을 멈춘다. 빈 샘플이나 호환되지 않는 데이터는 기준 포즈와 안내를 표시한다. 미리보기 입력과 재생 상태는 `.blendspace` 파일에 저장하지 않는다. 독립 Blend Space Editor의 샘플 편집과 Viewer의 3D 미리보기를 구분하여 사용한다.

이 미리보기 탭 추가 작업은 Debug x64 Runtime·Editor 빌드만 확인하며, 테스트 실행·추가 또는 에디터 실행을 통한 동작 검증은 포함하지 않는다.
