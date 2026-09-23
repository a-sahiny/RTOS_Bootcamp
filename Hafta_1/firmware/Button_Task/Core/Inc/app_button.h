/**
 * app_button.h
 *
 * Buton EXTI ISR'i (PI11, her iki kenar): debounce filtresi, t0, olay
 * kimligi, ring buffer slotu ve ButtonQueue'ya olay birakma.
 *
 * Filtre: bir kenar ancak onceki HERHANGI bir kenardan en az APP_DEBOUNCE_MS
 * sessizlik sonra geldiyse VE pin o an basili seviyedeyse "basma" olarak
 * kabul edilir. Birakma kenarlari da sessizlik suresini yeniledigi icin
 * birakma sirasindaki sekmeler yeni basma sayilmaz. t0, ISR'a girilen an
 * alinir ve yalnizca kenar kabul edilirse kullanilir.
 */
#ifndef APP_BUTTON_H
#define APP_BUTTON_H

void Button_Init(void);

/** START/RESET_STATS'ta: olay kimligini 1'den yeniden baslatir. */
void Button_ResetForScenario(void);

/** HAL_GPIO_EXTI_Callback'ten (ISR context) USER_BUTTON_PIN icin cagrilir. */
void Button_HandleEXTI(void);

#endif /* APP_BUTTON_H */
