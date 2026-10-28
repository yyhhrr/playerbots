# Playerbots
Bot AI Core from ike3 for cmangos classic, tbc and wotlk

This system brings the following features:
- Populate the open world with playerbots
- Populate BGs and Arenas with playerbots
- Use alt characters as playerbots
- Do any kind of PvE content (with some guidance on complex mechanics)
- Very detailed configurations of playerbot behaviors (for the min-maxers out there :D)
- Multiple commands to request playerbots do what you require

# How to install
## Compiling Code
If you're new to building CMaNGOS, check the official guide
https://github.com/cmangos/issues/wiki/Installation-Instructions

Important: to enable the playerbots you need to check it in cmake ( `BUILD_PLAYERBOTS` ✅ )

After successful build get aiplayerbot.conf file from "src/modules/Bots/playerbot/aiplayerbot%expansion.conf.dist" (based on expansion you use) and put it to the same folder where mangosd.conf and realmd.conf are, and remove ".dist" from its name

## Apply DB modifications
- Using the `InstallFullDB.sh` script:
  1. Execute the script once to generate the `InstallFullDB.config` file, after that close the script
  2. Edit `InstallFullDB.config` and add `PLAYERBOTS_DB="YES"` at the end and save it
  3. Depending on if you have a previous installation or want to do a fresh installation:
     - For a fresh install pick the option `4) Full installation`
     - To install only the playerbots DB pick the option `5) Advanced DB management` and then `8) Create and fill playerbots db`

- Manually:
  1.  Go to "src/modules/Bots/sql"
  2.  Apply .sql files from "characters" folder to characters database
  3.  Apply .sql files from "world" folder to world database
  
  **IMPORTANT**: There are several .sql files that are in a `vanilla`, `tbc` or `wotlk` folder. You should only use the files in the folder for the core expansion you are currently using.

After you complete all steps above you can check bots config and start your server. It'll take some time for the first time, as gear/characters for bots will be generated at first launch. Have fun! 🥳

## How to Use

### 法师远程输出与 OT 处理

法师默认保持远程施法距离，按现有控制分配使用变羊，三系群怪输出优先暴风雪。
在 10 码内有至少三个有效目标、其中存在实际追打法师的 OT 怪时，切换为奥术爆炸；
60 级也适用，变羊保护、法力和技能可用性仍需满足。近身目标不足三个时继续拉距。
远处的暴风雪、烈焰风暴目标不再阻止近身退让，冰环后也会尝试创造施法空间；
明确的停留命令和副本安全寻路仍然有效。

### 手动拾取与队长分配

在小队或团队频道发送 `拾取` 或 `loot`，同队机器人会扫描附近掉落并尝试移动、打开和拾取；
也可以私聊单个机器人。设置了聊天命令前缀时，仍需加上该前缀。

队长分配模式下，机器人也会尝试拾取金币、允许领取的任务物品，以及轮到自己或已释放的普通掉落。
超过分配品质门槛、必须由物品分配者发放的装备仍等待正常分配，不会自动拿走。
距离、敌人、背包空间、物品过滤和已启用的 `dungeon safe` 检查仍然生效。

机器人开始拾取后，普通跟随会短暂让行，取完后恢复跟随；玩家走远、进入战斗、
目标失效或移动长期没有进展时会结束本次拾取。打不开、没有实际收获的目标会短暂退避，
避免在尸体和玩家之间反复往返。只有等待分配或等待掷骰的物品不会引起反复空开尸体。

### 一键清空同队机器人背包

在小队或团队频道发送 `清空背包`，也支持英文 `clear bags`，同队机器人会各自清空背包。
私聊同队机器人时只清理该机器人。命令不带参数；设置了聊天命令前缀时需加上前缀。

会销毁主背包和四个随身包里的全部物品，包括备用装备、消耗品、材料和任务物品。
已穿戴装备、已装备的背包本体、银行和钥匙链保留，金币不变。交易、施法或背包物品使用中拒绝执行，需结束后重试。
每个机器人私聊反馈销毁的物品组数和总件数。只有同队真人可以发令，执行时会再次检查组队关系；
命令不会影响玩家角色，也不会清理未同队的机器人。

### 战歌峡谷协作策略（第一版）

旗手按职业、当前及最大血量、到旗帜的距离、法力、已学技能的冷却和资源状态评分，
德鲁伊优先，冰法和盗贼为机动替补，战士和骑士获得持盾及附近治疗支援加分。
队伍唯一治疗会扣分。预选主旗手及替补并保留约 25 秒，死亡、失去资格或明显劣势时更换，
普通技能冷却波动不会反复换人。两人优先保留抢旗任务，小队仍保留追回成员。
旗台优先让当前能拾旗的主、替补拿旗；两者未到场时由现场成员立即拾取。
掉旗允许各角色就近接力，不等远处主旗手；已有真人或机器人旗手继续持旗，不自动交接。

携旗后优先使用可用的变形、疾跑、闪现和幽魂狼等机动技能，保留急救治疗的优先级。
德鲁伊在近身压力和低血量时可转熊，形态切换有节流；法师闪现会检查朝向目标的视线及地形。
携旗者禁用会导致掉旗的免疫、潜行/隐形、假死、坐骑和传送法术及相关道具；机器人也不会
对真人或机器人旗手施放会掉旗的保护免疫。保留闪现、自由、吸收盾、树皮和盾墙。

战歌按实例和阵营保存抢旗、护旗、追回和持旗任务，每 2 秒更新人数，旗帜状态变化立即调整。
普通分工至少保持 30 秒，路线偏好按角色 GUID 固定，不再定时随机换岗或切换墓地路线。
己方持旗时优先安排治疗及附近成员护旗；敌方持有己方旗或己方旗掉地时安排追回，
人手不足时先保留基本护旗人数。附近真人会计入人数，机器人不会改变真人任务。

旗手限制主动选敌，开始返程时结束旧追击；仅允许对 12 码内直接
攻击自己的敌人自卫。到达交旗点或躲藏点后让出动作机会给治疗和自保。护旗者落后超过
35 码时优先重新靠近，旗手与护旗者不会为远处增益离队。

修复了专用追旗动作误要求机器人自己持旗、联盟隧道中间点被覆盖、联盟二楼上下跳点错误，
以及路线忽略移动失败的问题。失败后尝试向已分配目标寻路，不进入随机漫游。
此版本沿用战场旗手位置数据和预设躲藏点；尚未加入敌情侦察记忆或动态避敌路线。

### 阿拉希盆地协作策略（第一版）

机器人按战场实例和阵营统一分配据点任务，每 3 秒更新一次。分配会计入据点附近的真人队友、
机器人以及已经派出的支援；敌情只采用友军当前能看见且有视线的敌人。普通任务至少保持 30 秒，
战斗或开旗期间保持任务，紧急补防可以调整未交战成员。

己方据点优先安排至少一人留守；争夺中的据点有余员时安排两人开旗和掩护。
在途支援不算已经到岗，最后一名实际守卫不会提前离点。多人抵达同一旗帜时只选一名可行动的
机器人开旗；真人正在开旗时机器人掩护。开旗中断后重新选人，开旗后继续留守等待占领。

战斗中优先选择正在开旗的可攻击敌人。守点目标通常限制在旗帜周围 40 码，允许对 10 码内
直接攻击自己的敌人自卫；超出守点范围会尝试寻路返回。开旗掩护期间不追逐增益物品。
此版本尚未加入按资源比分决定保三点、职业编组或历史敌情侦察；实际路线和战斗效果需游戏内验证。

### 公会机器人立即上线

在游戏聊天栏输入 `.bot 公会上线`，也支持 `.bot 工会上线` 和 `.bot guildlogin`。
命令直接为当前公会中离线的随机机器人提交登录请求，不需要已有机器人在线，也不要求 GM。
只处理配置中机器人账号下的公会成员；普通玩家角色、正在被真人使用的账号、其他公会成员均跳过。
已在线的机器人不会重复登录，正在登录的机器人也不会重复提交请求。反馈中的“已提交”表示登录请求已发出，
实际进入游戏仍需完成数据库加载。命令只负责上线，不会自动组队或传送。

手动上线绕过随机登录间隔、每批登录数量和随机人口上限。为避免异步人口调度立刻将它们踢下线，
本次请求在内存中保留上线保护，持续 `AiPlayerbot.MinRandomBotInWorldTime` 秒（至少 60 秒）；
到期后恢复普通随机调度，显式下线命令仍然有效。服务端重启不会保留这项临时保护。
`AiPlayerbot.Enabled` 仍需开启，`AiPlayerbot.RandomBotAutologin` 可以关闭。

### 同队机器人直接召唤（Classic）

在队伍、团队聊天中发送 `summon` 或 `过来`，或私聊同队机器人。命令只对与发令者
处于同一队伍／团队的机器人有效；执行前会再次核对成员关系。
它直接把机器人传送到发令者的位置，死亡机器人满血复活，不需要集合石、旅店、
炉石、冷却或脱离战斗，并会中断施法和飞行路线。`AiPlayerbot.NonGmFreeSummon`
不再控制这条命令。配置了聊天命令前缀时，仍需加上该前缀。

Classic 核心还需应用 [配套补丁](patches/cmangos-classic-direct-summon.patch)，以在此次
传送期间跳过副本进入条件，并进入发令者所在的实例；不会修改 GM 权限或已有副本绑定。
传送在下一个安全的世界更新点执行并立即处理 ACK，不等待机器人接受召唤请求。
发令者正在过图时会等其地图状态稳定；离线目标不会被登录或召唤。

服务端测试注册了 `direct_summon_*` 用例，覆盖同地图／跨地图、死亡满血、战斗、
中文命令及非队友拒绝。需要运行中的测试服执行这些用例来验证游戏内行为。

- [List of Commands](https://docs.google.com/document/d/1xIdu5l5lAKLSKhqZ2Hb6vaU8qJgbbLwCw4MxmhCW_gI/edit#heading=h.vsmxe9r82yc7)
- [Playerbots Behavior AddOn](https://github.com/celguar/mangosbot-addon)
- [Playerbot Inventory AddOn](https://github.com/davidonete/mangosbot-EngBags)
- [Playerbots Discord Channel](https://discord.gg/vmjZUnPUdr)
