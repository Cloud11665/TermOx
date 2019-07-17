#include <memory>
#include <random>

#include <gtest/gtest.h>

#include <cppurses/system/detail/event_queue.new.hpp>
#include <cppurses/system/event.hpp>
#include <cppurses/system/events/delete_event.hpp>
#include <cppurses/system/events/focus_event.hpp>
#include <cppurses/system/events/paint_event.hpp>
#include <cppurses/widget/widgets/push_button.hpp>

namespace {
using namespace cppurses;

auto get_widg() -> Push_button&
{
    static Push_button w;
    return w;
}

auto make_event(Event::Type type) -> std::unique_ptr<Event>
{
    if (type == Event::Paint)
        return std::make_unique<Paint_event>(get_widg());
    if (type == Event::Delete)
        return std::make_unique<Delete_event>(get_widg(), nullptr);
    return std::make_unique<Focus_in_event>(get_widg());
}

/// Randomly generate a number between [first, last]
auto random_pick(int first, int last) -> int
{
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_int_distribution<> dist{first, last};
    return dist(gen);
}

auto make_rand_event() -> std::unique_ptr<Event>
{
    const auto choice = random_pick(0, 2);
    if (choice == 0)
        return make_event(Event::Paint);
    if (choice == 1)
        return make_event(Event::Delete);
    return make_event(Event::None);
}

}  // namespace

using namespace cppurses::detail;
using View = Event_queue::Filter_view;

TEST(EventQueue, GeneralUse)
{
    Event_queue queue{};
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::Paint));
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::Delete));
    queue.append(make_event(Event::Paint));
    queue.append(make_event(Event::Delete));
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::Delete));

    auto general_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::None}) {
        EXPECT_TRUE(event->type() != Event::Paint ||
                    event->type() != Event::Delete);
        ++general_count;
    }
    EXPECT_TRUE(general_count == 4);

    auto paint_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Paint}) {
        EXPECT_TRUE(event->type() == Event::Paint);
        ++paint_count;
    }
    EXPECT_TRUE(paint_count == 1);  // Not 2, because of append optimizations.

    auto delete_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Delete}) {
        EXPECT_TRUE(event->type() == Event::Delete);
        ++delete_count;
    }
    EXPECT_TRUE(delete_count == 3);
}

TEST(EventQueue, EventsRemovedOnUse)
{
    Event_queue queue{};
    constexpr auto count = 10'000;
    for (auto i = 0; i < count; ++i) {
        queue.append(make_rand_event());
    }

    auto paint_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Paint}) {
        ++paint_count;
    }
    const auto previous_paint_count = paint_count;
    for (std::unique_ptr<Event> event : View{queue, Event::Paint}) {
        ++paint_count;
    }
    EXPECT_TRUE(paint_count == previous_paint_count);
    View{queue, Event::Paint}.cleanup();

    auto delete_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Delete}) {
        ++delete_count;
    }
    const auto previous_delete_count = delete_count;
    for (std::unique_ptr<Event> event : View{queue, Event::Delete}) {
        ++delete_count;
    }
    EXPECT_TRUE(delete_count == previous_delete_count);
    View{queue, Event::Delete}.cleanup();

    auto general_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::None}) {
        ++general_count;
    }
    auto previous_general_count = general_count;
    for (std::unique_ptr<Event> event : View{queue, Event::None}) {
        ++general_count;
    }
    EXPECT_TRUE(general_count == previous_general_count);
    View{queue, Event::None}.cleanup();
}

TEST(EventQueue, EmptyQueue)
{
    Event_queue queue{};

    auto general_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::None}) {
        ++general_count;
    }
    EXPECT_TRUE(general_count == 0);

    auto paint_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Paint}) {
        ++paint_count;
    }
    EXPECT_TRUE(paint_count == 0);

    auto delete_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Delete}) {
        ++delete_count;
    }
    EXPECT_TRUE(delete_count == 0);
}

TEST(EventQueue, EmptyView)
{
    Event_queue queue{};
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::Paint));
    queue.append(make_event(Event::None));

    auto delete_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Delete}) {
        ++delete_count;
    }
    EXPECT_TRUE(delete_count == 0);
}

TEST(EventQueue, AppendWhileIterating)
{
    Event_queue queue{};
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::Paint));
    queue.append(make_event(Event::None));
    queue.append(make_event(Event::Delete));

    auto general_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::None}) {
        if (general_count < 2) {
            queue.append(make_event(Event::None));
            queue.append(make_event(Event::Paint));
        }
        ++general_count;
    }
    EXPECT_TRUE(general_count == 5);  // 3 original plus 2 appended general.

    auto paint_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Paint}) {
        ++paint_count;
    }
    EXPECT_TRUE(paint_count == 1);  // added ones are optimized out

    auto delete_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Delete}) {
        ++delete_count;
    }
    EXPECT_TRUE(delete_count == 1);
}

TEST(EventQueue, RemoveEventsOf)
{
    Push_button w;
    Event_queue queue{};
    queue.append(std::make_unique<Paint_event>(w));
    queue.append(make_event(Event::Paint));
    queue.append(make_event(Event::None));
    queue.append(std::make_unique<Focus_in_event>(w));

    View{queue, Event::None}.remove_events_of(&w);

    auto general_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::None}) {
        EXPECT_TRUE(&(event->receiver()) == &get_widg());
        ++general_count;
    }
    EXPECT_TRUE(general_count == 1);

    auto paint_count = 0;
    for (std::unique_ptr<Event> event : View{queue, Event::Paint}) {
        EXPECT_TRUE(&(event->receiver()) == &get_widg());
        ++paint_count;
    }
    EXPECT_TRUE(paint_count == 1);
}
