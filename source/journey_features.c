#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <switch.h>

#include "journey_features.h"
#include "debug.h"
#include "error.h"

enum {
  OFF_TSK_INTERNAL_DESTROY = 0x0416e280,
  OFF_TSK_CONSTRUCT_HELPER = 0x0416e550,
  OFF_TSK_GET_TEXT = 0x0416e870,
  OFF_TSK_SET_TEXT = 0x0416e8ac,
  OFF_TSK_GET_ACTIVE = 0x0416e92c,
  OFF_TSK_SET_ACTIVE = 0x0416e968,
  OFF_TSK_GET_STATUS = 0x0416e9ac,
  OFF_TSK_SET_CHARACTER_LIMIT = 0x0416e9e8,
  OFF_PROFILE_TEXT_INPUT_ELIGIBLE = 0x0214c634,
  OFF_SHOP_ON_START_CALLBACK_ARG = 0x02121e40,
  OFF_SHOP_ON_START_INIT_CALL = 0x02121e44,
  OFF_SHOP_ON_START_COROUTINE = 0x02121e54,
  OFF_SHOP_PAYMENT_STATUS_GUARD = 0x02121ec0,
  OFF_SHOP_GET_CATALOG_PRODUCT = 0x01fd63cc,
  OFF_SHOP_GET_LOCAL_PRODUCT = 0x01fd5800,
  OFF_SHOP_PURCHASE_CLICKED = 0x0211fb50,
  OFF_INVENTORY_SAVE = 0x01fc7728,
  OFF_INVENTORY_ADD_ITEM = 0x01fc7e34,
  OFF_ACCOUNT_DOWNLOAD_CONSENT = 0x0202a928,
  OFF_ADDITIONAL_FTUE_ASSETS = 0x0202bdc0,
  OFF_OBSERVABLE_RETURN_BOOL = 0x03f292b4,
};

enum {
  TSK_STATUS_VISIBLE = 0,
  TSK_STATUS_DONE = 1,
  TSK_STATUS_CANCELED = 2,
};

typedef struct {
  uint32_t keyboard_type;
  uint32_t autocorrection;
  uint32_t multiline;
  uint32_t secure;
  uint32_t alert;
  int32_t character_limit;
} KeyboardArguments;

typedef struct {
  void *klass;
  void *monitor;
  int32_t length;
  uint16_t first_char;
} ManagedString;

typedef void *(*Il2CppStringNewFn)(const char *text);
typedef void *(*Il2CppDomainGetFn)(void);
typedef const void **(*Il2CppDomainGetAssembliesFn)(const void *domain,
                                                    size_t *count);
typedef const void *(*Il2CppAssemblyGetImageFn)(const void *assembly);
typedef size_t (*Il2CppImageGetClassCountFn)(const void *image);
typedef void *(*Il2CppImageGetClassFn)(const void *image, size_t index);
typedef const char *(*Il2CppClassGetNameFn)(const void *klass);
typedef void *(*Il2CppClassGetDeclaringTypeFn)(const void *klass);
typedef void *(*Il2CppObjectNewFn)(void *klass);
typedef void (*Il2CppWriteBarrierFn)(void *object, void **field, void *value);

typedef void *(*GetShopProductFn)(void *shop_manager, void *product_id,
                                  void *method);
typedef int (*InventoryAddItemFn)(void *inventory, int item_type, void *item_id,
                                  int count, void *method);
typedef void (*InventorySaveFn)(void *inventory, void *method);
typedef void *(*ObservableReturnBoolFn)(int value, void *method);

static ObservableReturnBoolFn g_observable_return_bool;

static Il2CppStringNewFn g_string_new;
static char g_keyboard_text[0x1001];
static void *g_keyboard_managed_text;
static int g_keyboard_status = TSK_STATUS_CANCELED;
static int g_keyboard_active;
static int g_keyboard_character_limit = 500;
static uint64_t g_keyboard_cookie;

static Il2CppDomainGetFn g_domain_get;
static Il2CppDomainGetAssembliesFn g_domain_get_assemblies;
static Il2CppAssemblyGetImageFn g_assembly_get_image;
static Il2CppImageGetClassCountFn g_image_get_class_count;
static Il2CppImageGetClassFn g_image_get_class;
static Il2CppClassGetNameFn g_class_get_name;
static Il2CppClassGetDeclaringTypeFn g_class_get_declaring_type;
static Il2CppObjectNewFn g_object_new;
static Il2CppWriteBarrierFn g_write_barrier;
static void *g_billing_product_class;
static void *g_billing_catalog_price_class;
static void *g_free_price_text;
static GetShopProductFn g_get_shop_product;
static InventoryAddItemFn g_inventory_add_item;
static InventorySaveFn g_inventory_save;

static size_t append_utf8(char *out, size_t cap, size_t at, uint32_t cp) {
  if (cp <= 0x7f) {
    if (at + 1 < cap) out[at++] = (char)cp;
  } else if (cp <= 0x7ff) {
    if (at + 2 < cap) {
      out[at++] = (char)(0xc0 | (cp >> 6));
      out[at++] = (char)(0x80 | (cp & 0x3f));
    }
  } else if (cp <= 0xffff) {
    if (at + 3 < cap) {
      out[at++] = (char)(0xe0 | (cp >> 12));
      out[at++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[at++] = (char)(0x80 | (cp & 0x3f));
    }
  } else if (cp <= 0x10ffff && at + 4 < cap) {
    out[at++] = (char)(0xf0 | (cp >> 18));
    out[at++] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[at++] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[at++] = (char)(0x80 | (cp & 0x3f));
  }
  return at;
}

static void managed_string_to_utf8(const void *value, char *out, size_t cap) {
  if (!out || !cap) return;
  out[0] = 0;
  const ManagedString *str = value;
  if (!str || str->length <= 0 || str->length > 0x100000) return;

  const uint16_t *chars = &str->first_char;
  size_t at = 0;
  for (int32_t i = 0; i < str->length; ++i) {
    uint32_t cp = chars[i];
    if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < str->length) {
      const uint32_t low = chars[i + 1];
      if (low >= 0xdc00 && low <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        ++i;
      }
    }
    const size_t next = append_utf8(out, cap, at, cp);
    if (next == at && cp) break;
    at = next;
  }
  out[at] = 0;
}

static void refresh_managed_keyboard_text(void) {
  if (g_string_new) g_keyboard_managed_text = g_string_new(g_keyboard_text);
}

void journey_keyboard_set_result(const char *text, int canceled) {
  if (!canceled && text)
    snprintf(g_keyboard_text, sizeof g_keyboard_text, "%s", text);
  g_keyboard_active = 0;
  g_keyboard_status = canceled ? TSK_STATUS_CANCELED : TSK_STATUS_DONE;
  refresh_managed_keyboard_text();
}

static intptr_t nx_keyboard_construct(const KeyboardArguments *args,
                                      void *initial_text,
                                      void *placeholder,
                                      void *method) {
  (void)method;
  char initial[sizeof g_keyboard_text];
  char guide[0x101];
  managed_string_to_utf8(initial_text, initial, sizeof initial);
  managed_string_to_utf8(placeholder, guide, sizeof guide);
  snprintf(g_keyboard_text, sizeof g_keyboard_text, "%s", initial);

  int limit = args ? args->character_limit : 0;
  if (limit <= 0 || limit > 500) limit = 500;
  g_keyboard_character_limit = limit;
  g_keyboard_status = TSK_STATUS_VISIBLE;
  g_keyboard_active = 1;

  SwkbdConfig keyboard;
  Result rc = swkbdCreate(&keyboard, 0);
  if (R_SUCCEEDED(rc)) {
    if (args && args->secure)
      swkbdConfigMakePresetPassword(&keyboard);
    else
      swkbdConfigMakePresetDefault(&keyboard);

    if (args && (args->keyboard_type == 4 || args->keyboard_type == 5 ||
                 args->keyboard_type == 11))
      swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
    swkbdConfigSetStringLenMax(&keyboard, (uint32_t)limit);
    swkbdConfigSetInitialText(&keyboard, initial);
    if (guide[0]) swkbdConfigSetGuideText(&keyboard, guide);
    rc = swkbdShow(&keyboard, g_keyboard_text, sizeof g_keyboard_text);
    swkbdClose(&keyboard);
  }

  g_keyboard_active = 0;
  if (R_SUCCEEDED(rc)) {
    g_keyboard_status = TSK_STATUS_DONE;
    refresh_managed_keyboard_text();
  } else {
    snprintf(g_keyboard_text, sizeof g_keyboard_text, "%s", initial);
    g_keyboard_status = TSK_STATUS_CANCELED;
    refresh_managed_keyboard_text();
  }
  return (intptr_t)&g_keyboard_cookie;
}

static void nx_keyboard_destroy(intptr_t ptr, void *method) {
  (void)ptr;
  (void)method;
}

static void *nx_keyboard_get_text(void *self, void *method) {
  (void)self;
  (void)method;
  if (!g_keyboard_managed_text) refresh_managed_keyboard_text();
  return g_keyboard_managed_text;
}

static void nx_keyboard_set_text(void *self, void *value, void *method) {
  (void)self;
  (void)method;
  managed_string_to_utf8(value, g_keyboard_text, sizeof g_keyboard_text);
  g_keyboard_managed_text = value;
}

static int nx_keyboard_get_active(void *self, void *method) {
  (void)self;
  (void)method;
  return g_keyboard_active;
}

static void nx_keyboard_set_active(void *self, int active, void *method) {
  (void)self;
  (void)method;
  g_keyboard_active = active && g_keyboard_status == TSK_STATUS_VISIBLE;
}

static int nx_keyboard_get_status(void *self, void *method) {
  (void)self;
  (void)method;
  return g_keyboard_status;
}

static void nx_keyboard_set_character_limit(void *self, int limit, void *method) {
  (void)self;
  (void)method;
  if (limit > 0 && limit <= 500) g_keyboard_character_limit = limit;
}

static int nx_profile_text_input_eligible(void *self, void *method) {
  (void)self;
  (void)method;
  return 1;
}

static void patch_jump(uintptr_t base, uint32_t offset,
                       const uint32_t expected[4], void *target,
                       const char *name) {
  volatile uint32_t *entry = (volatile uint32_t *)(base + offset);
  for (unsigned i = 0; i < 4; ++i) {
    if (entry[i] != expected[i])
      fatal_error("Unsupported Journey build at %s (+0x%x word %u).",
                  name, offset, i);
  }
  const uintptr_t address = (uintptr_t)target;
  const uint32_t stub[4] = {
    0x58000050u,
    0xd61f0200u,
    (uint32_t)(address & 0xffffffffu),
    (uint32_t)(address >> 32),
  };
  if (so_patch_code((void *)entry, stub, sizeof stub) != 0)
    fatal_error("Could not install %s.", name);
}

static void patch_word(uintptr_t base, uint32_t offset, uint32_t expected,
                       uint32_t replacement, const char *name) {
  volatile uint32_t *site = (volatile uint32_t *)(base + offset);
  if (*site != expected)
    fatal_error("Unsupported Journey build at %s (+0x%x).", name, offset);
  if (so_patch_code((void *)site, &replacement, sizeof replacement) != 0)
    fatal_error("Could not install %s.", name);
}

static void install_keyboard(so_module *il2cpp) {
  const uintptr_t base = (uintptr_t)il2cpp->load_virtbase;
  g_string_new = (Il2CppStringNewFn)
      so_try_find_addr_rx(il2cpp, "il2cpp_string_new");
  if (!g_string_new) fatal_error("IL2CPP string creation export is missing.");

  static const uint32_t destroy_sig[4] = {
    0xf81e0ffe, 0xa9014ff4, 0x900042b4, 0xf9432281,
  };
  static const uint32_t construct_sig[4] = {
    0xf81d0ffe, 0xa90157f6, 0xa9024ff4, 0x900042b6,
  };
  static const uint32_t get_text_sig[4] = {
    0xf81e0ffe, 0xa9014ff4, 0x900042b4, 0xf9433a81,
  };
  static const uint32_t set_text_sig[4] = {
    0xa9be57fe, 0xa9014ff4, 0x900042b5, 0xf9433ea2,
  };
  static const uint32_t get_active_sig[4] = {
    0xf81e0ffe, 0xa9014ff4, 0x900042b4, 0xf9434681,
  };
  static const uint32_t set_active_sig[4] = {
    0xa9be57fe, 0xa9014ff4, 0x900042b5, 0xf9434aa2,
  };
  static const uint32_t get_status_sig[4] = {
    0xf81e0ffe, 0xa9014ff4, 0x900042b4, 0xf9434e81,
  };
  static const uint32_t set_limit_sig[4] = {
    0xa9be57fe, 0xa9014ff4, 0x900042b5, 0xf94352a2,
  };
  static const uint32_t profile_eligible_sig[4] = {
    0xf81e0ffe, 0xa9014ff4, 0xb0014353, 0xd0012bd4,
  };

  patch_jump(base, OFF_TSK_INTERNAL_DESTROY, destroy_sig,
             nx_keyboard_destroy, "TouchScreenKeyboard destroy hook");
  patch_jump(base, OFF_TSK_CONSTRUCT_HELPER, construct_sig,
             nx_keyboard_construct, "TouchScreenKeyboard constructor hook");
  patch_jump(base, OFF_TSK_GET_TEXT, get_text_sig,
             nx_keyboard_get_text, "TouchScreenKeyboard text getter");
  patch_jump(base, OFF_TSK_SET_TEXT, set_text_sig,
             nx_keyboard_set_text, "TouchScreenKeyboard text setter");
  patch_jump(base, OFF_TSK_GET_ACTIVE, get_active_sig,
             nx_keyboard_get_active, "TouchScreenKeyboard active getter");
  patch_jump(base, OFF_TSK_SET_ACTIVE, set_active_sig,
             nx_keyboard_set_active, "TouchScreenKeyboard active setter");
  patch_jump(base, OFF_TSK_GET_STATUS, get_status_sig,
             nx_keyboard_get_status, "TouchScreenKeyboard status getter");
  patch_jump(base, OFF_TSK_SET_CHARACTER_LIMIT, set_limit_sig,
             nx_keyboard_set_character_limit, "TouchScreenKeyboard limit setter");
  patch_jump(base, OFF_PROFILE_TEXT_INPUT_ELIGIBLE, profile_eligible_sig,
             nx_profile_text_input_eligible, "profile text-input eligibility");
}

static void set_managed_reference(void *object, size_t offset, void *value) {
  void **field = (void **)((char *)object + offset);
  if (g_write_barrier)
    g_write_barrier(object, field, value);
  else
    *field = value;
}

static int find_billing_classes(void) {
  if (g_billing_product_class && g_billing_catalog_price_class) return 1;

  void *domain = g_domain_get ? g_domain_get() : NULL;
  size_t assembly_count = 0;
  const void **assemblies = domain && g_domain_get_assemblies
      ? g_domain_get_assemblies(domain, &assembly_count) : NULL;
  void *billing_class = NULL;

  for (size_t a = 0; assemblies && a < assembly_count; ++a) {
    const void *image = g_assembly_get_image(assemblies[a]);
    const size_t class_count = image ? g_image_get_class_count(image) : 0;
    for (size_t i = 0; i < class_count; ++i) {
      void *klass = g_image_get_class(image, i);
      void *declaring = klass ? g_class_get_declaring_type(klass) : NULL;
      const char *outer = declaring ? g_class_get_name(declaring) : NULL;
      if (!outer || strcmp(outer, "Billing")) continue;
      if (!billing_class) billing_class = declaring;
      if (declaring != billing_class) continue;

      const char *name = g_class_get_name(klass);
      if (name && !strcmp(name, "Product"))
        g_billing_product_class = klass;
      else if (name && !strcmp(name, "CatalogPrice"))
        g_billing_catalog_price_class = klass;
    }
  }

  if (!g_billing_product_class || !g_billing_catalog_price_class) {
    return 0;
  }
  return 1;
}

static void *nx_get_catalog_product(void *shop_manager, void *product_id,
                                    void *method) {
  (void)shop_manager;
  (void)method;
  if (!product_id || !find_billing_classes()) return NULL;

  void *price = g_object_new(g_billing_catalog_price_class);
  void *product = g_object_new(g_billing_product_class);
  if (!price || !product) {
    return NULL;
  }
  if (!g_free_price_text) g_free_price_text = g_string_new("FREE");

  set_managed_reference(price, 0x10, g_string_new("NX"));
  *(uint64_t *)((char *)price + 0x18) = 0;
  set_managed_reference(price, 0x20, g_string_new("NX"));
  set_managed_reference(price, 0x28, g_free_price_text);

  set_managed_reference(product, 0x10, product_id);
  set_managed_reference(product, 0x18, product_id);
  *(int32_t *)((char *)product + 0x20) = 0;
  set_managed_reference(product, 0x28, product_id);
  set_managed_reference(product, 0x30, product_id);
  set_managed_reference(product, 0x38, price);
  set_managed_reference(product, 0x50, product_id);

  return product;
}

static void close_shop_popup(void *popup) {
  void *klass = popup ? *(void **)popup : NULL;
  if (!klass) return;
  typedef void (*CloseFn)(void *self, void *method);
  CloseFn close = *(CloseFn *)((char *)klass + 808);
  void *method = *(void **)((char *)klass + 816);
  if (close) close(popup, method);
}

static void nx_purchase_clicked(void *popup, void *product_id, void *method) {
  (void)method;
  if (!popup || !product_id) {
    debug_log("shop: ignored purchase with missing popup or product id");
    return;
  }

  void *shop_manager = *(void **)((char *)popup + 0x150);
  void *operation_manager = *(void **)((char *)popup + 0x148);
  void *inventory = operation_manager
      ? *(void **)((char *)operation_manager + 0x38) : NULL;
  void *shop_product = shop_manager
      ? g_get_shop_product(shop_manager, product_id, NULL) : NULL;
  void *bundle = shop_product ? *(void **)((char *)shop_product + 0x30) : NULL;
  const size_t count = bundle ? *(size_t *)((char *)bundle + 0x18) : 0;

  if (!inventory || !bundle || !count || count > 64) {
    debug_log("shop: could not grant purchase inventory=%p bundle=%p items=%u",
              inventory, bundle, (unsigned)count);
    return;
  }

  unsigned granted = 0;
  void **items = (void **)((char *)bundle + 0x20);
  for (size_t i = 0; i < count; ++i) {
    void *item = items[i];
    if (!item) continue;
    const int item_type = *(int32_t *)((char *)item + 0x10);
    void *item_id = *(void **)((char *)item + 0x18);
    const int amount = *(int32_t *)((char *)item + 0x20);
    if (item_type < 0 || amount <= 0) continue;
    g_inventory_add_item(inventory, item_type, item_id, amount, NULL);
    granted++;
  }

  if (!granted) {
    debug_log("shop: purchase bundle contained no grantable items");
    return;
  }
  g_inventory_save(inventory, NULL);
  debug_log("shop: granted and saved %u item entries", granted);
  close_shop_popup(popup);
}

static void *require_il2cpp_export(so_module *il2cpp, const char *name) {
  void *address = (void *)so_try_find_addr_rx(il2cpp, name);
  if (!address) fatal_error("Required IL2CPP export %s is missing.", name);
  return address;
}

static void install_offline_billing(so_module *il2cpp) {
  const uintptr_t base = (uintptr_t)il2cpp->load_virtbase;
  g_domain_get = (Il2CppDomainGetFn)
      require_il2cpp_export(il2cpp, "il2cpp_domain_get");
  g_domain_get_assemblies = (Il2CppDomainGetAssembliesFn)
      require_il2cpp_export(il2cpp, "il2cpp_domain_get_assemblies");
  g_assembly_get_image = (Il2CppAssemblyGetImageFn)
      require_il2cpp_export(il2cpp, "il2cpp_assembly_get_image");
  g_image_get_class_count = (Il2CppImageGetClassCountFn)
      require_il2cpp_export(il2cpp, "il2cpp_image_get_class_count");
  g_image_get_class = (Il2CppImageGetClassFn)
      require_il2cpp_export(il2cpp, "il2cpp_image_get_class");
  g_class_get_name = (Il2CppClassGetNameFn)
      require_il2cpp_export(il2cpp, "il2cpp_class_get_name");
  g_class_get_declaring_type = (Il2CppClassGetDeclaringTypeFn)
      require_il2cpp_export(il2cpp, "il2cpp_class_get_declaring_type");
  g_object_new = (Il2CppObjectNewFn)
      require_il2cpp_export(il2cpp, "il2cpp_object_new");
  g_write_barrier = (Il2CppWriteBarrierFn)
      require_il2cpp_export(il2cpp, "il2cpp_gc_wbarrier_set_field");
  g_get_shop_product = (GetShopProductFn)(base + OFF_SHOP_GET_LOCAL_PRODUCT);
  g_inventory_add_item = (InventoryAddItemFn)(base + OFF_INVENTORY_ADD_ITEM);
  g_inventory_save = (InventorySaveFn)(base + OFF_INVENTORY_SAVE);

  static const uint32_t catalog_sig[4] = {
    0xf81d0ffe, 0xa90157f6, 0xa9024ff4, 0xd0014ef5,
  };
  static const uint32_t purchase_sig[4] = {
    0xf81c0ffe, 0xa9015ff8, 0xa90257f6, 0xa9034ff4,
  };
  patch_jump(base, OFF_SHOP_GET_CATALOG_PRODUCT, catalog_sig,
             nx_get_catalog_product, "offline catalog hook");
  patch_jump(base, OFF_SHOP_PURCHASE_CLICKED, purchase_sig,
             nx_purchase_clicked, "offline purchase hook");

  patch_word(base, OFF_SHOP_ON_START_CALLBACK_ARG, 0xaa1403e1u, 0xaa1f03e1u,
             "local shop callback argument");
  patch_word(base, OFF_SHOP_ON_START_INIT_CALL, 0x97fff71eu, 0x9400000au,
             "local shop initializer");
  patch_word(base, OFF_SHOP_ON_START_COROUTINE, 0x948115bcu, 0xd503201fu,
             "online shop coroutine removal");
  patch_word(base, OFF_SHOP_PAYMENT_STATUS_GUARD, 0x340000e0u, 0x1400000eu,
             "offline shop status guard");
}

static void *nx_account_download_consent(void *self, void *method) {
  (void)self;
  (void)method;
  return g_observable_return_bool(1, NULL);
}

static void *nx_ftue_assets_ready(void *self, void *method) {
  (void)self;
  (void)method;
  return g_observable_return_bool(1, NULL);
}

static void install_offline_adventure(so_module *il2cpp) {
  const uintptr_t base = (uintptr_t)il2cpp->load_virtbase;
  g_observable_return_bool =
      (ObservableReturnBoolFn)(base + OFF_OBSERVABLE_RETURN_BOOL);

  static const uint32_t preflight_sig[4] = {
    0xf81c0ffe, 0xa9015ff8, 0xa90257f6, 0xa9034ff4,
  };
  patch_jump(base, OFF_ACCOUNT_DOWNLOAD_CONSENT, preflight_sig,
             nx_account_download_consent, "account download preflight hook");
  patch_jump(base, OFF_ADDITIONAL_FTUE_ASSETS, preflight_sig,
             nx_ftue_assets_ready, "FTUE asset preflight hook");
}

void journey_features_install(so_module *il2cpp) {
  debug_log("journey: installing native keyboard and offline feature patches");
  install_keyboard(il2cpp);
  install_offline_billing(il2cpp);
  install_offline_adventure(il2cpp);
  debug_log("journey: offline shop patches installed");
}
