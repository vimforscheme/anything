# ---- Layer 1: Config (通过文件名 'network' 体现) ----

 层级(Layer), Name (名称) Type (类型), Value (值)
Config：必须有名称（文件名），充当文件和隔离的命名空间。

Section：可有可无名称（具名或匿名），必须有类型，对功能进行逻辑分组。

Option/List：必须有名称（键），必须有值，存储具体的配置数据。

config interface 'lan'         # ---- Layer 2: Section (Type='interface', Name='lan') ----
    option proto 'static'      # ---- Layer 3: Option  (Name='proto', Value='static') ----
    list ipaddr '192.168.1.1'  # ---- Layer 3: List    (Name='ipaddr', Value='192.168.1.1') ----

config rule                    # ---- Layer 2: Section (Type='rule', Name=无/匿名) ----
    option target 'ACCEPT'     # ---- Layer 3: Option  (Name='target', Value='ACCEPT') ----



# Layer 1: Config (文件名 network)
{
    # Layer 2: Section (Name: lan)
    "lan": { 
        "__meta__": {"type": "interface", "anonymous": False}, # Layer 2: Section 的 Type
        # Layer 3: Option
        "proto": "static",  # Name: proto, Value: static
        "ipaddr": ["192.168.1.1"] 
    },
    
    # Layer 2: Section (Name: 内部生成的匿名ID，外部叫 @rule[0])
    "anon_12345": {
        "__meta__": {"type": "rule", "anonymous": True}, # Layer 2: Section 的 Type
        # Layer 3: Option
        "target": "ACCEPT" # Name: target, Value: ACCEPT
    }
}