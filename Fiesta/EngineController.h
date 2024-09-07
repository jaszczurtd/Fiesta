#ifndef ENGINE_CONTROLLER_H
#define ENGINE_CONTROLLER_H

class EngineController {
public:
    virtual void init() = 0;  
    virtual void process() = 0; 
    virtual ~EngineController() {}  
};

#endif // ENGINE_CONTROLLER_H