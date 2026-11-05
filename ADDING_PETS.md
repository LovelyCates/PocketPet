# 添加一只新宠物

最方便的方式：把角色文件夹、`pet.json` 或标准 PNG/WebP 精灵图拖到宠物身体上。程序会检查文件，复制进 EXE 同级的 `pets`，并加入角色菜单。单独导入标准图集时会自动生成 `pet.json`。已有同名角色不会被覆盖，导入失败时原来的宠物会继续显示。

也可以直接放文件，不用重启、不用重新编译：

```text
PocketPet.exe
pets/
  my-new-pet/
    pet.json
    spritesheet.webp
```

放好后重新打开「右键 → 换个伙伴」，菜单会重新扫描。可复制 EXE 旁的 `pet-template.json`，改名为 `pet.json`，修改角色名称和图片文件名。

## 哪些内容可以自动识别

| 图片或包 | 适配方式 |
| --- | --- |
| Codex v1，1536 × 1872 | 自动识别 8 列 × 9 行，播放九组标准动作 |
| Codex v2，1536 × 2288 | 自动识别 8 列 × 11 行，额外启用 16 个注视方向 |
| PNG 或 WebP | 按文件内容解码，保留透明通道 |
| 新名称、新目录 | 从 pet.json 读取名称；没有 collection 时放入「我的伙伴」 |
| 同级 pets 里的新角色 | 每次打开角色菜单自动发现 |
| 自定义网格图集 | 需要在 pet.json 中填写 frame 布局和 animations 动作表 |
| 普通单张立绘、GIF、视频 | 不能自动生成完整动作，需先制作成精灵图集 |

默认模板省略版本号，程序根据标准图集尺寸判断 v1/v2。九组动作分别是待机、右走、左走、挥手、跳跃、失落、等待、思考、检查；动作中的真实姿势和换装由素材本身提供。

自定义网格示例（256 × 128 图片，单格 64 × 64）：

```json
{
  "displayName": "新伙伴",
  "collection": "我的伙伴",
  "spritesheetPath": "sheet.png",
  "frame": {"width":64,"height":64,"columns":4,"rows":2},
  "animations": {
    "idle": {"frames":[0,1,2,3],"fps":6,"loop":true},
    "jumping": {"frames":[4,5,6,7],"fps":8,"loop":false,"fallback":"idle"}
  }
}
```

帧号从左到右、从上到下，自 0 开始。图片大小必须恰好等于单格尺寸乘网格数量。缺少的动作会回到待机，程序不会把一张静态立绘伪装成九套不同素材。
