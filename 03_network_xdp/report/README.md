# 03. Networking: Early Packet Drop with Generic XDP

## 📌 목표
전통적인 리눅스 네트워크 스택의 `sk_buff` 할당과 상위 계층 처리 비용을 분석하고, XDP(eXpress Data Path)로 이보다 앞선 지점에서 패킷을 필터링했을 때의 효과를 검증한다.

특정 포트를 대상으로 하는 UDP flood를 모사하고, 해당 패킷을 `XDP_DROP`으로 폐기하는 필터를 구현한다.

## 🛠️ Test Environment & Target Workload
* **Target Workload:** Python `socket` 라이브러리로 루프백(`127.0.0.1`) 9999번 포트에 UDP 패킷을 연속 전송하는 스크립트(`udp_flood.py`)를 사용한다.
* **Defense & Observability Tools:** 9999번 포트의 UDP 패킷에 `XDP_DROP`을 반환하는 C 기반 eBPF/XDP 프로그램(`xdp_drop_port.c`)을 사용한다.
  * `tcpdump`: 커널 네트워크 스택(AF_PACKET) 레벨에서의 패킷 도달 여부 관측.

## 📊 1. Baseline: 전통적인 커널 네트워크 스택의 한계 (방어막 부재)
XDP 프로그램을 부착하지 않은 상태에서 UDP 패킷을 전송하고 처리량을 측정한다.

* **관측 결과 (`tcpdump`):** 패킷이 AF_PACKET 관측 지점에 도달하여 로그에 기록된다.
* **전송 처리량:** 초당 약 **200,000~300,000 pkt/s**를 기록한다.
* **분석:** 루프백 환경에서는 송신 프로세스와 커널 네트워크 처리가 같은 호스트의 CPU를 공유한다. XDP가 없으면 패킷이 `sk_buff` 기반 IP/UDP 경로와 AF_PACKET 관측 지점을 통과하므로, 이 처리 비용이 송신 처리량에도 영향을 미친다.

![img1](before_guard.png)


## 📊 2. XDP Defense: 상위 스택 진입 전 패킷 폐기
루프백 인터페이스에 generic XDP(`xdpgeneric`)로 eBPF 프로그램을 부착하고, 9999번 포트의 UDP 패킷에 `XDP_DROP`을 반환하도록 설정한다. generic XDP는 native NIC 드라이버 경로가 아니라 커널의 generic 수신 경로에서 실행된다.

* **관측 결과 (`tcpdump`):** 패킷 로그가 관측되지 않았다. 이는 패킷이 AF_PACKET 탭에 도달하기 전 XDP 훅에서 폐기되었음을 나타낸다.
* **전송 처리량:** 초당 약 **610,000~620,000 pkt/s**를 기록하여 Baseline 범위의 약 2.0~3.1배에 해당한다.

![img2](after_guard.png)

## 💡 결론
본 실험에서 XDP 필터는 패킷의 상위 네트워크 스택 진입을 차단했고, 같은 루프백 조건에서 송신 처리량이 200,000~300,000 pkt/s에서 610,000~620,000 pkt/s로 증가했다.

1. **커널 보호 및 리소스 보존 (Resource Exhaustion 방어)**

   XDP 훅은 필터 대상 패킷이 `sk_buff` 기반 상위 스택을 통과하는 것을 방지한다. 루프백 환경에서 이 비용이 감소하면 같은 CPU를 공유하는 송신 스크립트가 더 많은 패킷을 전송할 수 있다. 따라서 송신량 증가는 방어 성능이 저하된 결과가 아니라 조기 폐기 경로의 비용이 낮아졌음을 시사한다.
   
2. **미션 크리티컬 시스템을 위한 차세대 보안/네트워킹 아키텍처**

   XDP는 불필요한 패킷을 조기에 제거하여 상위 스택과 애플리케이션의 부하를 줄일 수 있다. 다만 본 결과는 `lo`의 generic XDP와 단일 송신 프로세스를 사용한 마이크로벤치마크이므로, native/driver XDP의 성능이나 DDoS 방어 시 CPU 점유율을 직접 입증하지는 않는다. 실제 NIC에서의 처리량, 패킷 크기, CPU 점유율, 드롭률을 함께 측정하는 후속 실험이 필요하다.
