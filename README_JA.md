# ZMK Input Vector Acceleration

[![Test](https://github.com/amgskobo/zmk-input-vector-acceleration/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-vector-acceleration/actions/workflows/test.yml)

[English](README.md)

`zmk-input-vector-acceleration` は、相対ポインター移動用の ZMK 入力プロセッサーです。
X/Y を別々に加速せず、X/Y の合成ベクトルから1つの倍率を求めて両軸へ同じ倍率を
適用します。そのため、斜め移動の方向が軸ごとの非線形加速によって変形しません。

計算は整数・固定小数点だけで行います。状態と端数は入力リスナー／デバイスの
ストリームごとに独立しています。不正なruntime listener indexは変換せず通過し、
stream 0へaliasしません。レイヤーは参照せず、キーボード本体、トラック
パッドドライバー、キーマップ動作からの通知も不要です。

## 処理方法

同期された入力レポートごとに次の処理を行います。

1. レポート内の相対 X/Y 移動量を集計します。
2. `max(|x|, |y|) + 3/8 * min(|x|, |y|)` でベクトル長を近似します。
3. レポート間隔から counts/sec の速度を求めます。
4. 固定小数点の二次曲線で倍率を求めます。
5. 求めた共通倍率を次のレポートの X/Y 両方へ適用します。

1レポート遅れるのは意図した設計です。ZMK の入力プロセッサーには X と Y が別の
イベントとして届くため、レポート全体を保留・再送しない限り、先に来た軸から後の軸を
知ることはできません。直前レポートの倍率を使うことで、プロキシ入力デバイスや
ワークキューを追加せず、イベント順も維持できます。レポート間隔の確定には2レポート
必要なので、起動時と100 msを超える無操作後は最初の2レポートが1.0倍です。

ZMKはレイヤーoverrideをイベントごとに選び直します。Xと同期付きYの間でレイヤーが
変わると、フレームを閉じるsyncをこのプロセッサーが受け取れない場合があります。
sync前に同じ軸が再到着した場合は未完了フレームと判断し、次のレポートへ混ぜずに
破棄します。

## 設定

`config/west.yml` にモジュールを追加します。

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-vector-acceleration
      remote: amgskobo
      revision: main
```

シールドの overlay でノードを読み込みます。

```dts
#include <zmk-input-vector-acceleration/input_processor_vector_accel.dtsi>
```

公開パラメーターは次の4つだけです。倍率は1000分率、速度は X/Y 合成ベクトルの
counts/sec です。

```dts
&vector_accel {
    min-factor = <500>;   /* 速度0で0.5倍 */
    max-factor = <3200>;  /* max-speed以上で3.2倍 */
    unity-speed = <1200>; /* ここでちょうど1.0倍 */
    max-speed = <6000>;   /* ここでmax-factorに到達 */
};
```

相対 X/Y を生成するプロセッサーの後、加速後の移動を受け取る inertia などの前に
`&vector_accel` を配置します。

```dts
input-processors = <&zip_absolute_to_relative>,
                   <&vector_accel>,
                   <&zip_inertia>;
```

変更するのは `INPUT_EV_REL` の `INPUT_REL_X` と `INPUT_REL_Y` だけです。ボタン、
絶対座標、縦横ホイールはそのまま通過します。付属ノードでは ZMK の端数追跡を有効に
しているため、1.0倍未満で生じる1カウント未満の移動も失いません。

### パラメーター一覧

| プロパティ | 有効範囲 | 意味 |
| --- | ---: | --- |
| `min-factor` | 100～1000 | 速度0の倍率（1000分率） |
| `max-factor` | 1000～20000 | `max-speed` 以上の倍率（1000分率） |
| `unity-speed` | 1以上 | 倍率がちょうど1.0になるベクトル速度 |
| `max-speed` | `unity-speed` より大きい値 | `max-factor` に到達するベクトル速度 |

曲線は `min-factor` から1.0倍までが二次曲線、その後 `max-factor` までが二次曲線です。
値の関係が不正な場合は、理由を示してファームウェアのビルドを停止します。

## 実行時設定

devicetree の値は既定値です。`vector_accel_runtime.h` を使うと、キーボードを
使用したままカーブを変更できます。

```c
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

struct vector_accel_config config;

vector_accel_get_config(dev, &config);
config.max_factor = 4000;
vector_accel_set_config(dev, &config);   /* -EINVAL のときは何も変更しない */
```

`vector_accel_set_config()` は、devicetree の `BUILD_ASSERT` と同じ境界を
純粋コアの `vector_accel_config_valid()` で確認します。ビルド時に弾かれる値は
実行時にも弾かれ、プロセッサは直前の設定のまま動き続けます。

`CONFIG_SETTINGS` が有効で、後述の custom-settings 連携が無効な場合、
`vector_accel_set_config()` で変更した値は `vaccel/<インスタンス>` キーに保存され、
再起動後も残ります。保存値は各プロセッサが devicetree の既定値を読み込んだ後に
適用されます。構造体と一致しない値や境界外の値は無視されるため、プロセッサは
使用可能な devicetree のカーブを維持します。

custom-settings 連携が有効な場合は、そのレジストリだけが永続化を担当します。
レジストリ経由の変更はノード名ベースのキーへ保存され、同じ runtime API を通して
適用されます。`vector_accel_set_config()` を直接呼んだ場合も実行中のカーブは
変わりますが、レジストリと食い違う第2の保存値は作りません。

module自身が永続化を担当する構成では、debounce後のflash保存をZephyrの共有system work queueではなく
ZMKのlow-priority work queueで実行します。カーブ変更がBluetooth、split、watchdog、device PMの処理を
待たせることはありません。

### Studio クライアントから編集する

`CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_CUSTOM_SETTINGS=y` にすると、
4 つの値が [zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
に登録されます。これはモジュール横断のレジストリなので、それを描画する Studio
クライアントがこの値も描画します。宣言した型と範囲がウィジェットに反映される
ため、モジュール専用のページは不要です。既定値は devicetree の値です。
設定は `amgskobo__accel` subsystem の下に表示されます。subsystem 名も永続化される
設定名の一部なので、その長さだけ各ノード名に使える領域が減ります。

キーは、そのノードの devicetree 名にフィールドを続けた形です。

```
vector_accel.min_factor
vector_accel.max_factor
padstick_vector_accel.min_factor
padstick_vector_accel.max_factor
```

ノード名を使うのは意図的です。チェーンを描画するビューは、各リスナーの
プロセッサを devicetree からたどって段ごとに `const struct device *` を得ます。
その `->name` は `DEVICE_DT_NAME()`、すなわち `DT_NODE_FULL_NAME()` であり、
このキーを組み立てている文字列と同一です。したがって、ある段の設定とは
「その device 名で始まるキー」そのものであり、そのために登録する仕組みも、
モジュール間の取り決めも、基板側が devicetree に書く文字列も要りません。

手書きの識別子を使わないため、最も一般的な衝突は避けられます。ただし
`DT_NODE_FULL_NAME()` はノード自身の名前であってパスではないため、devicetree が
一意性を保証するのは同じ親を持つノード間だけです。別の親の下に同名ノードがある
場合はキーが衝突し得るため、モジュールは起動時に重複を検出してログへ記録します。

ノードを rename すると以前の保存値は孤児になります。また、RPCキーの48バイト
上限と、subsystemを含む永続化名の64バイト上限を両方ともビルド時に確認します。
長すぎる名前は切り詰めず、原因となるノード名を示してビルドを停止します。
この連携を有効にする場合、ノード名は19文字以内にしてください
（例: `pointer_accel`、`stick_accel`）。

このオプションには、カスタム Studio RPC プロトコルを持つ patched ZMK と、
`config/west.yml` の `zmk-feature-custom-settings` が必要です。既定は無効で、
本家 ZMK 向けのビルドでは一切コンパイルされません。`CONFIG_SETTINGS` が有効なら、
モジュールは自前の保存領域を使います。

`ZMK_CUSTOM_SETTING_RANGE_INT32` は devicetree の `BUILD_ASSERT` と同じ境界を
宣言するので、クライアント側で不正な値を送る前に弾けます。ただし
「max-speed は unity-speed より大きい」という関係は表現できないため、4 値は
まとめて適用され、`vector_accel_config_valid()` が最終判定をします。成立しない
組み合わせは適用されず、直前のカーブが動き続けます。

## テスト

計算とストリーム状態のコアはプラットフォーム非依存です。ZMKビルドと同系統の
コンテナーで、optimized、sanitizer、32-bitの契約テストを実行します。

```bash
bash ./tests/run-docker.sh
```

テスト対象は、ベクトルの対称性、曲線の上限・下限と単調性、固定小数点の端数、整数の
飽和、無操作時のリセット、1レポート遅延、入力ストリーム間の分離です。

統合テストでは、upstream ZMKに対する基本driver、DYA forkに対するcustom-settings
adapter、`native_sim`上のruntime APIと永続化、不正なdevicetree値の拒否、ARMボード
向けstandalone／splitファームウェアを確認します。

```bash
bash ./tests/run-integration-docker.sh
```

## ライセンス

[MIT](LICENSE)
