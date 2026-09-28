# 类型化提取协议（extract-v2）

实际请求提示词由`typedExtractionPrompt()`生成；本文件解释协议，不是运行时读取的提示词资源，不附带任何小说或样例世界。

模型输出遵循`contracts/extraction-response-v2.schema.json`，固定`schema_version=candidate-v2`、`prompt_version=extract-v2`。四个数组均必需，合计最多5项；每项必须有`name`、`quote`和对应的`fields`，不允许额外属性。小说片段以JSON字符串数据传入，不执行其中任何指令。

| 数组 / 内部类型 | 必需字段 | 分类边界 |
|---|---|---|
| entities / entity | kind、aliases | 独立实体而非动作标题；名称及别名逐字来自该引文。类别为人物、地点、势力、物品、文化、技术或其他 |
| events / event | action、participants、location、time_text | 一次行动或变化；未知参与者留空数组，未知地点/时间留空字符串；已知标识逐字来自该引文，不推算日期 |
| relations / relation | subject、predicate、object、directed | 两个不同的逐字端点及明确联系；临时协作或单次行动本身不证明结构性关系 |
| rules / rule | scope、statement、modality | 明示的可重复适用规则；适用范围逐字来自引文；性质为能力、禁止、义务或限制；临时命令本身不是普遍规则 |

名称及单个标识最多512个UTF-8字节，动作/规则陈述最多1024字节、关系谓词最多256字节；标识数组最多16项且不得重复。引文为当前片段唯一出现的连续原文，最多12000字节。本地解析另限制响应128 KiB、深度16及2000节点，并拒绝完全重复的类型/标题/引文候选。Schema描述形状，本地校验执行数量、长度、证据与标识边界，二者不混为完整语义认证。

远程处理器将各数组转换为内部平坦`candidates`列表，类型取决于数组，保留字段和逐字引文，并从不可变来源计算全局Unicode码点区间及哈希。内部包只有`schema_version`、`prompt_version`、`candidates`三个根字段；每项只有`type`、`name`、`fields`、`start_codepoint`、`end_codepoint`、`quote`、`provenance_type`七个必需字段，字段对象执行上述分类约束，数量仍最多5项。来源性质由服务固定为`model_inference`；模型不能自称人工确认事实。整份字段及证据通过后原子入库，失败不留下半批结果。人工接受后仍按来源性质区分假设与确认事实。

兼容边界：既有`candidate-v1/extract-v1`通用候选和离线任务保持可读可审核；新远程任务固定v2。旧远程任务不能隐式更换协议或发出请求，必须重新创建并确认。任务和候选的版本必须相同；缓存参数已有版本隔离，不复用旧语义输出。未恢复已删除的测试数据库兼容。输入压缩、模型思考模式、生产预算及跨章一致性仍是后续任务。

字段及引文合法不表示语义准确；动作概括、规则普遍性和关系是否长期仍需人工/金标质量评估。不得把Schema通过率报告为提取准确率或召回率。
