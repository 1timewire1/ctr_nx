/* JNI compatibility environment for Unity and the game libraries.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

#include <stdint.h>

extern void *fake_vm;  // JavaVM *
extern void *fake_env; // JNIEnv *

// set when the engine asks the activity to finish
extern volatile int jni_quit_requested;

void jni_init(void);
void jni_process_soft_keyboard(void);
void jni_process_offline_services(void);

/* Offline premium-offer fallback. The native game renders these two offer
 * cards but never reaches GoogleIapManager.purchase without Play Billing. */
void jni_offline_observe_log(const char *tag, const char *text);
int jni_offline_subscription_tap(float x, float y);

// the fake MyNativeActivity jobject handed to ANativeActivity.clazz
void *jni_make_activity_object(void);

// fake Java object / string constructors
void *jni_make_string(const char *utf);
void *jni_make_object(const char *label);
const char *jni_string_utf(void *jstr);

// Stable Android-style locale name (for example "fr_FR") selected from libnx.
const char *jni_locale_name(void);

#endif
