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

## 수동 빌드

```bash
cd /Users/waynelee/framespace
source <emsdk_path>/emsdk_env.sh
emcmake cmake -S . -B build -G Ninja
cmake --build build
```

## 현재 상태

- WebGPU 디바이스/서피스 초기화
- 프레임 루프 실행
- 배경 클리어 렌더링

## 다음 단계

- 카메라/투영 행렬 시스템
- 장면 오브젝트 관리
- 스냅샷(색+깊이) 캡처
- 프레임 배치/경계 통과 물리 연동
