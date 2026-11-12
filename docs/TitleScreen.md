# The Basement 타이틀 화면

- 배경은 사용자가 제공한 지하실 이미지를 그대로 복사한 Background.png입니다.
- 제목은 두 번째 시안 TitleWordmark-v2.png이며, 왼쪽 어두운 영역에 메뉴와 함께 표시합니다. 투명 배경과 원본 비율을 유지합니다.
- 화면 비율이 달라도 배경 비율을 유지하며 화면을 채웁니다.
- 버튼은 낡고 습기 먹은 무광 회벽·석재 질감의 CellarButton.png입니다.
- 마우스를 올리면 밝은 CellarButtonHover.png로 바뀝니다.
- 버튼 글자는 이미지에 포함하지 않고 기존 기본 폰트로 따로 그립니다.
- 게임 시작: 기존 LoadScene으로 Assets/Scenes/Demo/06_Game.scene을 엽니다.
- 종료: 실행용 게임은 종료하고 에디터에서는 PIE 세션만 종료합니다.
- 에디터에서는 Assets/Scenes/Demo/05_TheBasement.scene을 열고 Play를 누릅니다.
- 실행용 게임은 Demo/05_TheBasement.scene부터 시작합니다.
- 기존 타이틀 입력 바인딩이 포함된 엔진 빌드가 필요합니다.
  이번 배경·버튼·제목 변경 자체는 Lua와 이미지 자산 변경입니다.
- Demo/05_TheBasement.scene, TitleScreen.lua, Assets/UI/Title, Demo/06_Game.scene을 함께 배포합니다.

## 버튼 이미지

원본 투명 PNG를 보존합니다. TitleScreen.lua의 FaceWidth/FaceHeight/Left/Top은
이미지 안의 실제 버튼 영역을 나타냅니다. 이 값을 이용해 투명 여백을 제외한
버튼 그림과 클릭 영역이 맞도록 표시합니다.

생성 방식과 프롬프트는 TitleUIAssets.md에 기록했습니다.
