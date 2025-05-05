#ifndef ENGINE_GAUGE_H
#define ENGINE_GAUGE_H

class Gauge {
public:
    virtual void redraw(void) = 0;  
    virtual int getBaseX(void) = 0; 
    virtual int getBaseY(void) = 0;
    virtual ~Gauge() {}  
};

#endif // ENGINE_GAUGE_H
