import UIAbility from '@ohos.app.ability.UIAbility';
import window from '@ohos.window';
import hilog from '@ohos.hilog';

export default class EntryAbility extends UIAbility {
  onCreate(): void {
    hilog.info(0x0000, 'CommuteDebug', 'Ability onCreate');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(0x0000, 'CommuteDebug', 'loadContent failed %{public}d', err.code);
      }
    });
  }
}
