#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H

LVPA_NAMESPACE_START

class ProgressBar
{
public:
    void Step(void);
    void Update(bool force = false);
    void Print(void);
    void Reset(void);
    inline void PartialFix(void) { done2 += done; done = 0; Update(); }
    inline void Finalize(void) { done2 = 0; done = total; _perc = 100; Print(); }
    ProgressBar(uint32 total = 0);
    ~ProgressBar();

    uint32 total, done, done2;
    std::string msg;
    
private:
    uint32 _perc, _oldperc;
};

LVPA_NAMESPACE_END

#endif
