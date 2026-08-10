/**
 * CommuteAgentService — location/sensor dump + proactive ticks (WGS84).
 * @syscap SystemCapability.Multimedia.CommuteAgentService
 */
declare namespace commuteagentservice {
  function CheckServiceAbility(): number;
  function InitService(): string;
  function HelloWorld(): string;
  function DestroyService(): string;
  function StartSensorCollection(): number;
  function StopSensorCollection(): number;
  /** JSON snapshot: acc/gyro/mag/rv/location/baro/wifi/ble/cells. */
  function GetRecentAccFrames(): string;
  function StartLocationCollection(): number;
  function StopLocationCollection(): number;
  function DrainPdrResults(): string;
  function ClearPdrResults(): number;
  function GetPdrDiag(): string;
  /** JSON includes pdrTrajectoryDumpDir (same session dir as sensor CSV), pdrSavedSegmentCount, pdrSavedSegments. */
  function GetLeaveCarState(): string;
  /** JSON: start point reverse-geocode (OSM db on SA). */
  function GetStartPlaceInfo(): string;
  /** Start/stop WiFi scan collection on SA. */
  function StartWifiCollection(): number;
  function StopWifiCollection(): number;
  /** Start/stop BLE scan collection on SA. */
  function StartBleCollection(): number;
  function StopBleCollection(): number;
  /** Start/stop cellular (network) info collection on SA. */
  function StartCellCollection(): number;
  function StopCellCollection(): number;
  /**
   * Product debug timeline for visualization HAP.
   * JSON: { ok, status:{scene,home_relation,...}, events:[{seq,t_ms,type,title,detail}], ... }
   * event types: SCENE | PUSH | OUTSIDE | LABEL | LLM_START | LLM_DONE
   */
  function GetProductDebugTimeline(): string;
  /** Clear in-memory debug timeline events. */
  function ClearProductDebugTimeline(): number;
}

export default commuteagentservice;
