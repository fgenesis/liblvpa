#include "LVPAInternal.h"
#include "ProgressBar.h"
#include "LVPATools.h"

LVPA_NAMESPACE_START

ProgressBar::~ProgressBar()
{
    printf("\n");
    fflush(stdout);
}

ProgressBar::ProgressBar(uint32 totalcount /* = 0 */)
: total(totalcount), done(0), done2(0), _perc(0), _oldperc(0)
{
    Print();
}

void ProgressBar::Step(void)
{
    ++done;
    Update();
}

void ProgressBar::Update(bool force /* = false */)
{
    if(total)
        _perc = ((done + done2) * 100) / total;
    else
        _perc = 0;

    if(_perc > 100)
        _perc = 100; // HACK

    if(force || _oldperc != _perc)
    {
        _oldperc = _perc;
        Print();
    }
}

void ProgressBar::Reset(void)
{
    done = done2 = 0;
    uint32 w = GetConsoleWidth() - 1;
    for(uint32 i = 0; i < w; ++i)
        putchar(' ');
    putchar('\r');
}

void ProgressBar::Print(void)
{
    uint32 divider;
    if(msg.length())
    {
        divider = 3;
        printf("\r%s [", msg.c_str());
    }
    else
    {
        divider = 2;
        printf("\r[");
    }
    
    uint32 i = 0, L = _perc / divider; // _perc is max 100, so L can be max. 50 or 33, depends on divider
    for( ; i < L; i++)
        putchar('=');
    uint32 rem = 100 / divider;
    for( ; i < rem; i++) // fill up remaining space
        putchar(' ');
    printf("] %3u%% (%u/%u)\r", _perc, done + done2, total);
    fflush(stdout);
}

LVPA_NAMESPACE_END
