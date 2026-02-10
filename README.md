# framespace

WebGPU + WASM(C++) 기반 프로토타입 시작 템플릿입니다.

## 사전 요구사항

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) 설치
- CMake 3.20+
- Ninja (권장)
- WebGPU 지원 브라우저 (Chrome/Edge 최신 버전 권장)

## 빠른 시작

```bash
cd /Users/waynelee/framespace
./scripts/build.sh <emsdk_root_path>
./scripts/serve.sh
```

예시:

```bash
./scripts/build.sh $HOME/dev/emsdk
./scripts/serve.sh
```

브라우저에서 `http://localhost:8080/framespace.html` 접속.

## 조작법 (현재 프로토타입)

- 캔버스 클릭: 마우스 포인터 락
- 마우스 이동: 시점 회전
- `W A S D`: 이동
- `Shift`: 빠르게 이동

## 현재 상태

- WebGPU 디바이스/서피스 초기화
- 프레임 루프 실행
- 깊이 버퍼 포함 3D 큐브 렌더링
- FPS 독립 카메라 이동/시점 제어

## 다음 단계

- 스냅샷(색+깊이) 캡처
- 사진 프레임 배치 및 로컬 공간화
- 경계 통과 물리 전환
