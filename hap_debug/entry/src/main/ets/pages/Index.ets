import commuteagentservice from '@ohos.commuteagentservice';

interface DebugStatus {
  scene?: string;
  home_relation?: string;
  company_relation?: string;
  score_home?: number;
  score_company?: number;
  eta_leave_s?: number;
  dist_home_m?: number;
  should_service?: boolean;
  has_gps?: boolean;
  last_intent?: string;
  last_llm_status?: string;
  last_llm_at_ms?: number;
}

interface AuditItem {
  t_ms?: number;
  audit_id?: string;
  message?: string;
  changes?: Object;
}

interface LlmResult {
  status?: string;
  reason?: string;
  settle_label?: string;
  response_summary?: string;
  audits?: AuditItem[];
}

interface DebugEvent {
  seq: number;
  t_ms: number;
  type: string;
  title: string;
  detail: string;
  result?: LlmResult;
}

interface TimelinePayload {
  ok?: boolean;
  error?: string;
  status?: DebugStatus;
  events?: DebugEvent[];
}

@Entry
@Component
struct Index {
  @State statusText: string = 'connecting…';
  @State scene: string = '—';
  @State homeRel: string = '—';
  @State companyRel: string = '—';
  @State scoreLine: string = '—';
  @State etaLine: string = '—';
  @State llmLine: string = '—';
  @State events: DebugEvent[] = [];
  @State pollOk: boolean = false;
  @State showDetail: boolean = false;
  @State detailTitle: string = '';
  @State detailStatus: string = '';
  @State detailReason: string = '';
  @State detailLabel: string = '';
  @State detailSummary: string = '';
  @State detailAudits: AuditItem[] = [];
  private timerId: number = -1;

  aboutToAppear(): void {
    this.refresh();
    this.timerId = setInterval(() => {
      this.refresh();
    }, 1000);
  }

  aboutToDisappear(): void {
    if (this.timerId >= 0) {
      clearInterval(this.timerId);
      this.timerId = -1;
    }
  }

  private formatTime(ms: number): string {
    if (!ms || ms <= 0) {
      return '--:--:--';
    }
    const d = new Date(ms);
    const hh = d.getHours().toString().padStart(2, '0');
    const mm = d.getMinutes().toString().padStart(2, '0');
    const ss = d.getSeconds().toString().padStart(2, '0');
    return `${hh}:${mm}:${ss}`;
  }

  private colorForType(type: string): string {
    if (type === 'PUSH') {
      return '#F59E0B';
    }
    if (type === 'LABEL') {
      return '#22C55E';
    }
    if (type === 'OUTSIDE') {
      return '#38BDF8';
    }
    if (type === 'LLM_START' || type === 'LLM_DONE') {
      return '#A78BFA';
    }
    return '#94A3B8';
  }

  private openEvent(ev: DebugEvent): void {
    if (ev.type !== 'LLM_DONE' && ev.type !== 'LLM_START') {
      return;
    }
    const r = ev.result;
    this.detailTitle = ev.title;
    this.detailStatus = r?.status ?? ev.title;
    this.detailReason = r?.reason ?? '';
    this.detailLabel = r?.settle_label ?? '';
    this.detailSummary = r?.response_summary ?? ev.detail;
    // Show newest typed decision first.
    const audits = r?.audits ?? [];
    this.detailAudits = audits.slice().reverse();
    this.showDetail = true;
  }

  private closeDetail(): void {
    this.showDetail = false;
  }

  private refresh(): void {
    if (this.showDetail) {
      return;
    }
    try {
      const raw = commuteagentservice.GetProductDebugTimeline();
      const data = JSON.parse(raw) as TimelinePayload;
      if (!data.ok) {
        this.pollOk = false;
        this.statusText = data.error ?? 'sa error';
        return;
      }
      this.pollOk = true;
      this.statusText = 'live · tap PersonalizeInvoke for Agent decision';
      const st = data.status ?? {};
      this.scene = st.scene ?? '—';
      this.homeRel = st.home_relation ?? '—';
      this.companyRel = st.company_relation ?? '—';
      const sh = st.score_home ?? 0;
      const sc = st.score_company ?? 0;
      this.scoreLine = `H ${sh.toFixed(2)}  C ${sc.toFixed(2)}`;
      const eta = st.eta_leave_s ?? -1;
      const dist = st.dist_home_m ?? -1;
      this.etaLine = `ETA ${eta >= 0 ? eta.toFixed(0) + 's' : '—'}  dist ${dist >= 0 ? dist.toFixed(0) + 'm' : '—'}`;
      this.llmLine = `${st.last_llm_status ?? '—'}  ${this.formatTime(st.last_llm_at_ms ?? 0)}`;
      const list = data.events ?? [];
      this.events = list.slice().reverse();
    } catch (e) {
      this.pollOk = false;
      this.statusText = 'NAPI unavailable / parse error';
    }
  }

  private clearTimeline(): void {
    try {
      commuteagentservice.ClearProductDebugTimeline();
      this.refresh();
    } catch (e) {
      this.statusText = 'clear failed';
    }
  }

  @Builder
  DetailSheet() {
    Column() {
      Row() {
        Text('Agent 个性化结果')
          .fontSize(18)
          .fontWeight(FontWeight.Bold)
          .fontColor('#F8FAFC')
        Blank()
        Button('关闭')
          .height(32)
          .fontSize(12)
          .backgroundColor('#334155')
          .onClick(() => {
            this.closeDetail();
          })
      }
      .width('100%')
      .padding({ bottom: 12 })

      Text(`状态  ${this.detailStatus}`)
        .fontSize(14)
        .fontColor('#C4B5FD')
      if (this.detailReason.length > 0) {
        Text(`触发  ${this.detailReason}${this.detailLabel.length > 0 ? ' / ' + this.detailLabel : ''}`)
          .fontSize(13)
          .fontColor('#94A3B8')
          .margin({ top: 4 })
      }

      Text('结构化决策审计')
        .fontSize(15)
        .fontWeight(FontWeight.Medium)
        .fontColor('#E2E8F0')
        .margin({ top: 8, bottom: 8 })

      if (this.detailAudits.length === 0) {
        Text('无 submit_agent_analysis 记录')
          .fontSize(13)
          .fontColor('#64748B')
      } else {
        ForEach(this.detailAudits, (a: AuditItem, idx: number) => {
          Text(a.message ?? '')
            .fontSize(13)
            .fontColor('#CBD5E1')
            .margin({ bottom: 6 })
        }, (a: AuditItem, idx: number) => `${a.audit_id}_${idx}`)
      }

      Text('模型回复摘要')
        .fontSize(15)
        .fontWeight(FontWeight.Medium)
        .fontColor('#E2E8F0')
        .margin({ top: 8, bottom: 8 })
      Text(this.detailSummary.length > 0 ? this.detailSummary : '—')
        .fontSize(12)
        .fontColor('#94A3B8')
    }
    .width('100%')
    .padding(16)
    .backgroundColor('#1E293B')
  }

  build() {
    Stack() {
      Column() {
        Row() {
          Text('Commute Debug')
            .fontSize(22)
            .fontWeight(FontWeight.Bold)
            .fontColor('#F8FAFC')
          Blank()
          Text(this.pollOk ? '● LIVE' : '○ OFF')
            .fontSize(14)
            .fontColor(this.pollOk ? '#4ADE80' : '#F87171')
        }
        .width('100%')
        .padding({ left: 16, right: 16, top: 16, bottom: 8 })

        Text(this.statusText)
          .fontSize(12)
          .fontColor('#94A3B8')
          .padding({ left: 16, bottom: 8 })

        Column() {
          Text(`Scene  ${this.scene}`)
            .fontSize(18)
            .fontWeight(FontWeight.Medium)
            .fontColor('#E2E8F0')
          Text(`Home ${this.homeRel} · Company ${this.companyRel}`)
            .fontSize(14)
            .fontColor('#CBD5E1')
            .margin({ top: 6 })
          Text(this.scoreLine)
            .fontSize(13)
            .fontColor('#94A3B8')
            .margin({ top: 4 })
          Text(this.etaLine)
            .fontSize(13)
            .fontColor('#94A3B8')
            .margin({ top: 2 })
          Text(`LLM  ${this.llmLine}`)
            .fontSize(13)
            .fontColor('#C4B5FD')
            .margin({ top: 6 })
        }
        .width('100%')
        .alignItems(HorizontalAlign.Start)
        .padding(16)
        .margin({ left: 12, right: 12 })
        .backgroundColor('#1E293B')
        .borderRadius(12)

        Row() {
          Text('Timeline')
            .fontSize(16)
            .fontColor('#E2E8F0')
            .fontWeight(FontWeight.Medium)
          Blank()
          Button('Clear')
            .height(32)
            .fontSize(12)
            .backgroundColor('#334155')
            .onClick(() => {
              this.clearTimeline();
            })
        }
        .width('100%')
        .padding({ left: 16, right: 16, top: 16, bottom: 8 })

        List({ space: 8 }) {
          ForEach(this.events, (ev: DebugEvent) => {
            ListItem() {
              Row() {
                Column()
                  .width(4)
                  .height('100%')
                  .backgroundColor(this.colorForType(ev.type))
                  .borderRadius(2)
                Column() {
                  Row() {
                    Text(ev.type)
                      .fontSize(12)
                      .fontColor(this.colorForType(ev.type))
                      .fontWeight(FontWeight.Bold)
                    Blank()
                    Text(this.formatTime(ev.t_ms))
                      .fontSize(11)
                      .fontColor('#64748B')
                  }
                  .width('100%')
                  Text(ev.title)
                    .fontSize(15)
                    .fontColor('#F1F5F9')
                    .margin({ top: 4 })
                  Text(ev.detail)
                    .fontSize(12)
                    .fontColor('#94A3B8')
                    .margin({ top: 4 })
                  if (ev.type === 'LLM_DONE') {
                    Text(ev.result ? '点击查看个性化决策 ›' : '无 result 载荷')
                      .fontSize(11)
                      .fontColor('#A78BFA')
                      .margin({ top: 6 })
                  }
                }
                .layoutWeight(1)
                .padding({ left: 10, right: 8, top: 10, bottom: 10 })
                .alignItems(HorizontalAlign.Start)
              }
              .width('100%')
              .backgroundColor('#1E293B')
              .borderRadius(10)
              .onClick(() => {
                this.openEvent(ev);
              })
            }
          }, (ev: DebugEvent) => `${ev.seq}`)
        }
        .layoutWeight(1)
        .padding({ left: 12, right: 12, bottom: 16 })
      }
      .width('100%')
      .height('100%')
      .backgroundColor('#0F172A')

      if (this.showDetail) {
        Column() {
          Blank()
            .layoutWeight(1)
            .width('100%')
            .backgroundColor('#00000099')
            .onClick(() => {
              this.closeDetail();
            })
          Scroll() {
            this.DetailSheet()
          }
          .height('70%')
          .width('100%')
          .backgroundColor('#1E293B')
          .borderRadius({ topLeft: 16, topRight: 16 })
        }
        .width('100%')
        .height('100%')
      }
    }
  }
}
