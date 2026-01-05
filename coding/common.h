//连接符
#define DEFINE_LIST_NODE(type) \
    typedef struct type##_node { \
        struct type##_node *next; \
        type *data; \
    } type##_node_t;

DEFINE_LIST_NODE(int)  // 自动生成 struct int_node 和 int_node_t
DEFINE_LIST_NODE(float) // 自动生成 struct float_node 和 float_node_t

//如果__VA_ARGS__ 为空，它会自动吃掉前面的那个逗号
#define LOG(fmt, ...)  printf("[INFO] " fmt, ##__VA_ARGS__)

// 变为字符串 "v"
#define PRINT_VAR(v)  printf(#v " = %d\n", v)

//类型安全的泛型
#define MIN(x, y) ({ \
    typeof(x) _x = (x); \
    typeof(y) _y = (y); \
    (void) (&_x == &_y); \
    _x < _y ? _x : _y; \
})

//防止展开错误
#define LOG_AND_FREE(p) \
    do { \
        printf("freeing %p\n", p); \
        free(p); \
    } while(0)

