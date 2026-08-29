#!/usr/bin/env python3
"""
Regenerates src/fonts/cjk_font.h — the Simplified-Chinese fallback face for
Camillia-mt, mirroring tools/gen_emoji_font.py.

Source: Noto Sans SC (SIL OFL 1.1), Regular weight.
Subset: the DEFAULT_CHARS most frequent Simplified-Chinese characters
        (frequency ranks from Jun Da's Modern Chinese Character Frequency
        List, embedded below) + CJK punctuation + fullwidth forms.

WHY FREQUENCY-RANKED, AND WHY SO FEW CHARACTERS
-----------------------------------------------
Every board profile shares the same dual-OTA partition layout with 3.125 MB
app slots (partitions.csv / partitions_16mb_fs.csv). Measured baselines
(firmware with emoji but without CJK): tdeck ~2.85 MB, cardputer-cap ~2.65 MB
— the worst board leaves only ~420 KB of headroom before the linker's
checkprogsize gate trips. The first attempt took the first 5,000 CJK Unified
codepoints in *unicode order* (~1.0 MB); it compiled but overflowed every
profile (cardputer 113%, tdeck 119%). Unicode order is block order (radicals),
not usage order, so it also bought the least common coverage per byte.

This list is ranked by real-world usage instead: 1,500 characters cover
roughly 94-95% of everyday Simplified-Chinese text for ~0.3 MB of flash.
Rarer characters render as the missing-glyph box. To trade size for coverage,
raise DEFAULT_CHARS (or pass N on the command line) and regenerate — no flash
layout changes needed, just keep tdeck's worst case under ~97% of 3,276,800
bytes. The embedded list holds the top 2,000; beyond that, re-rank from
https://lingua.mtsu.edu/chinese-computing/statistics/ (Jun Da).

Drops the layout tables tiny_ttf can't use (GSUB/GPOS/GDEF/morx/kern/FFTM/MATH).

Usage: python3 tools/gen_cjk_font.py path/to/NotoSansSC-Regular.otf [N]
Output: src/fonts/cjk_font.h
"""
import sys
import os
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

DEFAULT_CHARS = 1500

# Top-2,000 Modern Chinese characters by frequency (Jun Da, lingua.mtsu.edu),
# ordered most-frequent first. Wrapped at 50 characters per line.
FREQ_RANKED_CJK = (
    "的一是不了在人有我他这个们中来上大为和国地到以说时要就出会可也你对生能而子那得于着下自之年过发后作里"
    "用道行所然家种事成方多经么去法学如都同现当没动面起看定天分还进好小部其些主样理心她本前开但因只从想实"
    "日军者意无力它与长把机十民第公此已工使情明性知全三又关点正业外将两高间由问很最重并物手应战向头文体政"
    "美相见被利什二等产或新己制身果加西斯月话合回特代内信表化老给世位次度门任常先海通教儿原东声提立及比员"
    "解水名真论处走义各入几口认条平系气题活尔更别打女变四神总何电数安少报才结反受目太量再感建务做接必场件"
    "计管期市直德资命山金指克许统区保至队形社便空决治展马科司五基眼书非则听白却界达光放强即像难且权思王象"
    "完设式色路记南品住告类求据程北边死张该交规万取拉格望觉术领共确传师观清今切院让识候带导争运笑飞风步改"
    "收根干造言联持组每济车亲极林服快办议往元英士证近失转夫令准布始怎呢存未远叫台单影具罗字爱击流备兵连调"
    "深商算质团集百需价花党华城石级整府离况亚请技际约示复病息究线似官火断精满支视消越器容照须九增研写称企"
    "八功吗包片史委乎查轻易早曾除农找装广显吧阿李标谈吃图念六引历首医局突专费号尽另周较注语仅考落青随选列"
    "武红响虽推势参希古众构房半节土投某案黑维革划敌致陈律足态护七兴派孩验责营星够章音跟志底站严巴例防族供"
    "效续施留讲型料终答紧黄绝奇察母京段依批群项故按河米围江织害斗双境客纪采举杀攻父苏密低朝友诉止细愿千值"
    "仍男钱破网热助倒育属坐帝限船脸职速刻乐否刚威毛状率甚独球般普怕弹校苦创假久错承印晚兰试股拿脑预谁益阳"
    "若哪微尼继送急血惊伤素药适波夜省初喜卫源食险待述陆习置居劳财环排福纳欢雷警获模充负云停木游龙树疑层冷"
    "洲冲射略范竟句室异激汉村哈策演简卡罪判担州静退既衣您宗积余痛检差富灵协角占配征修皮挥胜降阶审沉坚善妈"
    "刘读啊超免压银买皇养伊怀执副乱抗犯追帮宣佛岁航优怪香著田铁控税左右份穿艺背阵草脚概恶块顿敢守酒岛托央"
    "户烈洋哥索胡款靠评版宝座释景顾弟登货互付伯慢欧换闻危忙核暗姐介坏讨丽良序升监临亮露永呼味野架域沙掉括"
    "舰鱼杂误湾吉减编楚肯测败屋跑梦散温困剑渐封救贵枪缺楼县尚毫移娘朋画班智亦耳恩短掌恐遗固席松秘谢鲁遇康"
    "虑幸均销钟诗藏赶剧票损忽巨炮旧端探湖录叶春乡附吸予礼港雨呀板庭妇归睛饭额含顺输摇招婚脱补谓督毒油疗旅"
    "泽材灭逐莫笔亡鲜词圣择寻厂睡博勒烟授诺伦岸奥唐卖俄炸载洛健堂旁宫喝借君禁阴园谋宋避抓荣姑孙逃牙束跳顶"
    "玉镇雪午练迫爷篇肉嘴馆遍凡础洞卷坦牛宁纸诸训私庄祖丝翻暴森塔默握戏隐熟骨访弱蒙歌店鬼软典欲萨伙遭盘爸"
    "扩盖弄雄稳忘亿刺拥徒姆杨齐赛趣曲刀床迎冰虚玩析窗醒妻透购替塞努休虎扬途侵刑绿兄迅套贸毕唯谷轮库迹尤竞"
    "街促延震弃甲伟麻川申缓潜闪售灯针哲络抵朱埃抱鼓植纯夏忍页杰筑折郑贝尊吴秀混臣雅振染盛怒舞圆搞狂措姓残"
    "秋培迷诚宽宇猛摆梅毁伸摩盟末乃悲拍丁赵硬麦蒋操耶阻订彩抽赞魔纷沿喊违妹浪汇币丰蓝殊献桌啦瓦莱援译夺汽"
    "烧距裁偏符勇触课敬哭懂墙袭召罚侠厅拜巧侧韩冒债曼融惯享戴童犹乘挂奖绍厚纵障讯涉彻刊丈爆乌役描洗玛患妙"
    "镜唱烦签仙彼弗症仿倾牌陷鸟轰咱菜闭奋庆撤泪茶疾缘播朗杜奶季丹狗尾仪偷奔珠虫驻孔宜艾桥淡翼恨繁寒伴叹旦"
    "愈潮粮缩罢聚径恰挑袋灰捕徐珍幕映裂泰隔启尖忠累炎暂估泛荒偿横拒瑞忆孤鼻闹羊呆厉衡胞零穷舍码赫婆魂灾洪"
    "腿胆津俗辩胸晓劲贫仁偶辑邦恢赖圈摸仰润堆碰艇稍迟辆废净凶署壁御奉旋冬矿抬蛋晨伏吹鸡倍糊秦盾杯租骑乏隆"
    "诊奴摄丧污渡旗甘耐凭扎抢绪粗肩梁幻菲皆碎宙叔岩荡综爬荷悉蒂返井壮薄悄扫敏碍殖详迪矛霍允幅撒剩凯颗骂赏"
    "液番箱贴漫酸郎腰舒眉忧浮辛恋餐吓挺励辞艘键伍峰尺昨黎辈贯侦滑券崇扰宪绕趋慈乔阅汗枝拖墨胁插箭腊粉泥氏"
    "彭拔骗凤慧媒佩愤扑龄驱惜豪掩兼跃尸肃帕驶堡届欣惠册储飘桑闲惨洁踪勃宾频仇磨递邪撞拟滚奏巡颜剂绩贡疯坡"
    "瞧截燃焦殿伪柳锁逼颇昏劝呈搜勤戒驾漂饮曹朵仔柔俩孟腐幼践籍牧凉牲佳娜浓芳稿竹腹跌逻垂遵脉貌柏狱猜怜惑"
    "陶兽帐饰贷昌叙躺钢沟寄扶铺邓寿惧询汤盗肥尝匆辉奈扣廷澳嘛董迁凝慰厌脏腾幽怨鞋丢埋泉涌辖躲晋紫艰魏吾慌"
    "祝邮吐狠鉴曰械咬邻赤挤弯椅陪割揭韦悟聪雾锋梯猫祥阔誉筹丛牵鸣沈阁穆屈旨袖猎臂蛇贺柱抛鼠瑟戈牢逊迈欺吨"
    "琴衰瓶恼燕仲诱狼池疼卢仗冠粒遥吕玄尘冯抚浅敦纠钻晶岂峡苍喷耗凌敲菌赔涂粹扁亏寂煤熊恭湿循暖糖赋抑秩帽"
    "哀宿踏烂袁侯抖夹昆肝擦猪炼恒慎搬纽纹玻渔磁铜齿跨押怖漠疲叛遣兹祭醉拳弥斜档稀捷肤疫肿豆削岗晃吞宏癌肚"
    "隶履涨耀扭坛拨沃绘伐堪仆郭牺歼墓雇廉契拼惩捉覆刷劫嫌瓜歇雕闷乳串娃缴唤赢莲霸桃妥瘦搭赴岳嘉舱俊址庞耕"
    "锐缝悔邀玲惟斥宅添挖呵讼氧浩羽斤酷掠妖祸侍乙妨贪挣汪尿莉悬唇翰仓轨枚盐览傅帅庙芬屏寺胖璃愚滴疏萧姿颤"
    "丑劣柯寸扔盯辱匹俱辨饿蜂哦腔郁溃谨糟葛苗肠忌溜鸿爵鹏鹰笼丘桂滋聊挡纲肌茨壳痕碗穴膀卓贤卧膜毅锦欠哩函"
    "茫昂薛皱夸豫胃舌剥傲拾窝睁携陵哼棉晴铃填饲渴吻扮逆脆喘罩卜炉柴愉绳胎蓄眠竭喂傻慕浑奸扇柜悦拦诞饱乾泡"
)

if len(sys.argv) not in (2, 3):
    sys.exit("Usage: python3 tools/gen_cjk_font.py path/to/NotoSansSC-Regular.otf [N]")

src = sys.argv[1]
n_chars = int(sys.argv[2]) if len(sys.argv) == 3 else DEFAULT_CHARS
if n_chars > len(FREQ_RANKED_CJK):
    sys.exit(
        f"N={n_chars} exceeds the embedded frequency list ({len(FREQ_RANKED_CJK)}). "
        "Re-rank from https://lingua.mtsu.edu/chinese-computing/statistics/ and "
        "extend FREQ_RANKED_CJK first."
    )
out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src", "fonts", "cjk_font.h"))

f = TTFont(src)

# Pin variable-font axis if present (Noto Sans SC.otf is static, but stay safe).
if "fvar" in f:
    instantiateVariableFont(f, {"wght": 400}, inplace=True)

cmap = f.getBestCmap()

# Frequency-ranked Han characters, then CJK punctuation + fullwidth forms, plus
# the handful of halfwidth marks Chinese text routinely carries (curly quotes,
# dashes, ellipsis, middle dot). Only codepoints that actually have glyphs in
# the source font survive.
cps = [ord(c) for c in FREQ_RANKED_CJK[:n_chars]]
cps += list(range(0x3000, 0x303F + 1))          # CJK punctuation
cps += list(range(0xFF00, 0xFFEF + 1))          # fullwidth forms
cps += [0x00B7, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026]
cps = sorted(set(c for c in cps if c in cmap))

opts = subset.Options()
opts.drop_tables += ["GSUB", "GPOS", "GDEF", "morx", "kern", "FFTM", "MATH"]
opts.layout_features = []
opts.name_IDs = []
opts.glyph_names = False
opts.hinting = False
opts.desubroutinize = True
ss = subset.Subsetter(options=opts)
ss.populate(unicodes=cps)
ss.subset(f)

import io
buf = io.BytesIO()
f.save(buf)
data = buf.getvalue()

os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as h:
    h.write("// Auto-generated by tools/gen_cjk_font.py. Do not hand-edit.\n")
    h.write("// Noto Sans SC (SIL OFL 1.1), Regular.\n")
    h.write("// Rendered at runtime by lv_tiny_ttf as a SECOND fallback after emoji.\n")
    h.write("#pragma once\n#include <stdint.h>\n\n")
    h.write(f"// {len(cps)} codepoints (top-{n_chars} frequency-ranked CJK + punctuation "
            f"+ fullwidth), {len(data)} bytes\n")
    h.write("// Approx flash footprint when paired with emoji_font.h:\n")
    h.write(f"//   emoji: ~2.4MB   cjk: {len(data)/1024:.0f}KB   total: "
            f"{(len(data)+2_400_000)/1024/1024:.2f}MB\n")
    h.write("// Both fallback faces live in flash; tinyTTF rasterizes on demand.\n")
    h.write("const uint8_t kCjkFontData[] = {\n")
    for i in range(0, len(data), 20):
        h.write("  " + ",".join(str(b) for b in data[i:i + 20]) + ",\n")
    h.write("};\n")
    h.write(f"const unsigned kCjkFontDataLen = {len(data)}u;\n")
print(f"wrote {out}: {len(data)} bytes, {len(cps)} codepoints")
