/**
 * Local typing stub for DevEco — keep in sync with
 * sa_service/interfaces/declaration/api/@ohos.commuteagentservice.d.ts
 */
declare namespace commuteagentservice {
  function CheckServiceAbility(): number;
  function InitService(): string;
  function HelloWorld(): string;
  function DestroyService(): string;
  function StartSensorCollection(): number;
  function StopSensorCollection(): number;
  function GetRecentAccFrames(): string;
  function StartLocationCollection(): number;
  function StopLocationCollection(): number;
  function DrainPdrResults(): string;
  function ClearPdrResults(): number;
  function GetPdrDiag(): string;
  function GetLeaveCarState(): string;
  function GetStartPlaceInfo(): string;
  function StartWifiCollection(): number;
  function StopWifiCollection(): number;
  function StartBleCollection(): number;
  function StopBleCollection(): number;
  function StartCellCollection(): number;
  function StopCellCollection(): number;
  function GetProductDebugTimeline(): string;
  function ClearProductDebugTimeline(): number;
}

export default commuteagentservice;
