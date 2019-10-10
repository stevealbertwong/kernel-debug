
#define bool _Bool

typedef unsigned int uint32_t;


void vm_swap_init(void);
uint32_t vm_swap_flush_kpage_to_disk(void* kpage);
bool vm_swap_read_kpage_from_disk(uint32_t bitmap_index, void *kpage);
void vm_swap_free(uint32_t bitmap_index);


