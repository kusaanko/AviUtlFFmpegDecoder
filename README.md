# AviUtlFFmpegDecoder
 AviUtl用のFFmpegを使用した入力プラグインです。  
 現在開発途中のもので動作が不安定な可能性があります。

# 特徴
- インデックスファイルを作らない
- 高速なシーク
- 高速な再生
- 好きなFFmpegを使用できる
- ハードウェアデコーダーが使用できる
- カット点の再生が高速

インデックスファイルを作らないことが大きな特徴です。  
インデックスファイルを作らないことで高速なシークを達成しています。  
しかしインデックスファイルを作らないことによるデメリットもあります。  
一部ファイルで正常にシークできないことがあります。  
その場合はL-SMASH Works等他の入力プラグインを使用してください。  

# このプラグインを使うのが適している人
- 普段OBS等のデスクトップレコーダーを使用している
- 頻繁に動画ファイルをカットする
- 精度より速度を求める

# 注意
この入力プラグインは高速にシーク・読み込みをすることを目的に作成しています。  
そのためある程度の精度が失われる可能性があります。  
実際に確認したところ動画ファイルによっては音声が実際に測った値ではありませんが16msほどずれる可能性があります。  
また、映像のずれは今の所ありませんでした。  
また、一部動画ファイルでシークができないファイルがあります。  
特にデジカメは予め正常にシークできるか確認してください。  
正確なシークを求める場合やシークできないファイルがある場合はL-SMASH Worksを使用してください。  

10bitカラーの動画ファイルに対応するかどうかは使用するFFmpegによって変わります。  

# インストール方法
1. https://github.com/kusaanko/AviUtlFFmpegDecoder/releases よりauiファイルをダウンロードします
1. ダウンロードしたauiファイルをaviutl.exeと同じディレクトリかPluginsフォルダに配置します
1. お好きなFFmpegの32bit Sharedファイルをダウンロードします
1. FFmpegのdllファイルをaviutl.exeと同じディレクトリに配置します
1. AviUtlの入力プラグイン優先順位よりFFmpeg Decoderを他の動画・音声入力プラグインより上、他のプラグインより下に移動します。

ファイル構造  
aviutl  
├Plugins/  
│  └AviUtlFFmpegDecoder.aui  
├aviutl.exe  
├avcodec-58.dll  
├avdevice-58.dll  
├avfilter-7.dll  
├avformat-58.dll  
├avresample-4.dll  
├avutil-56.dll  
├postproc-55.dll  
├swresample-3.dll  
└swscale-5.dll  
※FFmpegのバージョンによりdllファイルの数・ファイル名が異なる可能性があります。  

FFmpegのdllには https://github.com/kusaanko/FFmpeg-Auto-Build が使用可能です。  
その他のFFmpegも使用可能です。  
よくわからない方は https://github.com/kusaanko/FFmpeg-Auto-Build/releases からwin32_gpl_nx.x.x_shared_日付.zip を使用してください。  

# 既知のバグ
- ファイル->開くから動画ファイルを開いた場合、もしくはAviUtlのメインウィンドウに動画をドラッグ・アンド・ドロップして動画ファイルを読み込むんだ場合に正常に再生できません。拡張編集プラグインより読み込んでください。

# ハードウェアデコーダーを使用する
AviUtlのメニューバー>ファイル>環境設定>入力プラグインの設定>FFmpeg Decoder from Ropimerの設定をクリックします。  
出てきたダイアログのテキストボックスに使用したいデコーダー名を入力します。  
例:H.264のデコードをNvidiaのグラフィックスボードでデコードしたい
H.264のデコーダー名がh264であるため、これをNvidiaのハードウェアデコーダー名であるh264_cuvidに変更するように設定します。  

```
h264=h264_cuvid
```
左辺値に置換前のデコーダー名、右辺値に置換後のデコーダー名を指定します。  
他のデコーダーも同様です。以下のような指定ができます。 
- Nvidiaの場合

```
h264=h264_cuvid
hevc=hevc_cuvid
vp8=vp8_cuvid
vp9=vp9_cuvid
```

- Intelグラフィックスの場合

```
h264=h264_qsv
hevc=hevc_qsv
vp8=vp8_qsv
vp9=vp9_qsv
```
現在AMDでのデコードはサポートしていません。  
また、使用するFFmpegによってハードウェアデコーダーを内蔵していない可能性があります。

# YUY2
このプラグインはYUY2に対応しています。  
FFmpeg Decoderの設定より設定が変更可能です。  
デフォルトではYUY2への変換が有効になっています。  
チェックを外すことで無効にできます。無効にした場合、全てRGB(24bit)へ変換します。  
ほとんどの動画ファイルはYUVが使用されているのでYUY2への変換は有効が推奨です。  
また、RGBで記録されている動画ファイルはYUV2に変換せずRGBで読み込みます。

# スケーリングアルゴリズム
YUV420からYUV422、YUV444からYUV422等変換するときにどのように処理するかアルゴリズムを選択できます。  
デフォルトではBICUBIC(バイキュービック法)が設定されています。  
解像度を変更する際のアルゴリズムではありません。  