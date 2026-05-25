# scrubimg

PNG/JPEG 画像をデコードし、ピクセルだけを新しい画像として書き直すメタデータ削除ツールです。

## build

```sh
sudo apt-get install build-essential libpng-dev libjpeg-dev
make
```

## use

```sh
./scrubimg input.png output.png
./scrubimg input.jpg output.jpg
./scrubimg -q 95 input.jpg output.jpg
```

## behavior

- PNG: tEXt, zTXt, iTXt, tIME, eXIf, iCCP, gAMA, sRGB, cHRM, pHYs, bKGD, private chunks などを書きません。
- JPEG: EXIF, XMP, ICC, COM, サムネイル等を書きません。
- JPEG は不可逆圧縮なので、再エンコード後の画素値が完全一致する保証はありません。
- EXIF Orientation は適用しません。Orientation が必要だった画像は、メタデータ削除後に見た目の向きが変わることがあります。
