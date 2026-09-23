#include "../components/senses/include/touch_fsm.h"
#include <cassert>
#include <iostream>

using namespace buddy;

void test_hold10s() {
    TouchGesturesFSM fsm(100, 115, true); // baseline 100, threshold 115, raises
    
    // Initial stabilization
    assert(fsm.update(100, 0) == TouchGesturesFSM::Action::NONE);

    // Touch down (needs 2 confirms)
    assert(fsm.update(120, 10) == TouchGesturesFSM::Action::NONE);
    assert(fsm.update(120, 20) == TouchGesturesFSM::Action::DOWN);

    // Hold for just under 10s
    assert(fsm.update(120, 9999) == TouchGesturesFSM::Action::NONE);

    // Hit 10s mark
    assert(fsm.update(120, 10020) == TouchGesturesFSM::Action::HOLD_10S);
    
    // Ensure HOLD_10S doesn't fire again
    assert(fsm.update(120, 10040) == TouchGesturesFSM::Action::NONE);

    // Release after hold. Should NOT fire PET or POKE
    assert(fsm.update(100, 15000) == TouchGesturesFSM::Action::NONE);
    assert(fsm.update(100, 15020) == TouchGesturesFSM::Action::NONE);
}

void test_poke_and_pet() {
    TouchGesturesFSM fsm(100, 115, true); 
    
    // POKE: touch down, then release quickly (<400ms)
    assert(fsm.update(120, 10) == TouchGesturesFSM::Action::NONE);
    assert(fsm.update(120, 20) == TouchGesturesFSM::Action::DOWN);
    assert(fsm.update(100, 200) == TouchGesturesFSM::Action::NONE); // 1 confirm
    assert(fsm.update(100, 210) == TouchGesturesFSM::Action::POKE); // 2 confirms

    // PET: touch down, hold (>400ms, <10000ms), release
    assert(fsm.update(120, 300) == TouchGesturesFSM::Action::NONE);
    assert(fsm.update(120, 310) == TouchGesturesFSM::Action::DOWN);
    assert(fsm.update(100, 1000) == TouchGesturesFSM::Action::NONE);
    assert(fsm.update(100, 1010) == TouchGesturesFSM::Action::PET);
}

void test_debounce_noise() {
    TouchGesturesFSM fsm(100, 115, true); 
    
    // Noise spike (only 1 sample above threshold)
    assert(fsm.update(120, 10) == TouchGesturesFSM::Action::NONE);
    // Followed by normal reading
    assert(fsm.update(100, 20) == TouchGesturesFSM::Action::NONE);
    
    // Ensure not touching
    assert(!fsm.is_touching());
}

int main() {
    test_hold10s();
    test_poke_and_pet();
    test_debounce_noise();

    std::cout << "All TouchGesturesFSM tests passed!" << std::endl;
    return 0;
}
