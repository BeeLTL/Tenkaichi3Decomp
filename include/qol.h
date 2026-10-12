#ifndef QOL_H
#define QOL_H

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct QoLSettings {
        int skipintro; /* 1 = skips the intro */
        int skiplogos; /* 1 = skips the logos */
    } QoLSettings;

    extern QoLSettings qol;

#ifdef __cplusplus
}
#endif

#endif /* QOL_H */