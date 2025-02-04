/*
 * Copyright (c) 2020, Till Mayer <till.mayer@web.de>
 * Copyright (c) 2021, Gunnar Beutner <gbeutner@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Game.h"
#include "Helpers.h"
#include <AK/Debug.h>
#include <AK/QuickSort.h>
#include <LibGUI/Painter.h>
#include <LibGfx/Font.h>
#include <LibGfx/Palette.h>
#include <time.h>

REGISTER_WIDGET(Hearts, Game);

namespace Hearts {

Game::Game()
{
    srand(time(nullptr));

    m_delay_timer = Core::Timer::create_single_shot(0, [this] {
        advance_game();
    });

    constexpr int card_overlap = 20;
    constexpr int outer_border_size = 15;
    constexpr int player_deck_width = 12 * card_overlap + Card::width;
    constexpr int player_deck_height = 12 * card_overlap + Card::height;
    constexpr int text_height = 15;
    constexpr int text_offset = 5;

    m_players[0].first_card_position = { (width - player_deck_width) / 2, height - outer_border_size - Card::height };
    m_players[0].card_offset = { card_overlap, 0 };
    m_players[0].name_position = {
        (width - player_deck_width) / 2 - 50, height - outer_border_size - text_height - text_offset,
        50 - text_offset, text_height
    };
    m_players[0].name_alignment = Gfx::TextAlignment::BottomRight;
    m_players[0].name = "Gunnar";
    m_players[0].is_human = true;
    m_players[0].taken_cards_target = { width / 2 - Card::width / 2, height };

    m_players[1].first_card_position = { outer_border_size, (height - player_deck_height) / 2 };
    m_players[1].card_offset = { 0, card_overlap };
    m_players[1].name_position = {
        outer_border_size, (height - player_deck_height) / 2 - text_height - text_offset,
        Card::width, text_height
    };
    m_players[1].name_alignment = Gfx::TextAlignment::BottomLeft;
    m_players[1].name = "Paul";
    m_players[1].taken_cards_target = { -Card::width, height / 2 - Card::height / 2 };

    m_players[2].first_card_position = { width - (width - player_deck_width) / 2 - Card::width, outer_border_size };
    m_players[2].card_offset = { -card_overlap, 0 };
    m_players[2].name_position = {
        width - (width - player_deck_width) / 2 + text_offset, outer_border_size + text_offset,
        Card::width, text_height
    };
    m_players[2].name_alignment = Gfx::TextAlignment::TopLeft;
    m_players[2].name = "Simon";
    m_players[2].taken_cards_target = { width / 2 - Card::width / 2, -Card::height };

    m_players[3].first_card_position = { width - outer_border_size - Card::width, height - (height - player_deck_height) / 2 - Card::height };
    m_players[3].card_offset = { 0, -card_overlap };
    m_players[3].name_position = {
        width - outer_border_size - Card::width, height - (height - player_deck_height) / 2 + text_offset,
        Card::width, text_height
    };
    m_players[3].name_alignment = Gfx::TextAlignment::TopRight;
    m_players[3].name = "Lisa";
    m_players[3].taken_cards_target = { width, height / 2 - Card::height / 2 };
};

Game::~Game()
{
}

void Game::setup(String player_name)
{
    m_players[0].name = move(player_name);

    NonnullRefPtrVector<Card> deck;

    dbgln_if(HEARTS_DEBUG, "=====");
    dbgln_if(HEARTS_DEBUG, "Resetting game");

    stop_animation();

    m_trick.clear_with_capacity();
    m_trick_number = 0;

    for (int i = 0; i < Card::card_count; ++i) {
        deck.append(Card::construct(Card::Type::Clubs, i));
        deck.append(Card::construct(Card::Type::Spades, i));
        deck.append(Card::construct(Card::Type::Hearts, i));
        deck.append(Card::construct(Card::Type::Diamonds, i));
    }

    for (auto& player : m_players) {
        player.hand.clear_with_capacity();
        player.cards_taken.clear_with_capacity();
        for (uint8_t i = 0; i < Card::card_count; ++i) {
            auto card = deck.take(rand() % deck.size());
            if constexpr (!HEARTS_DEBUG) {
                if (&player != &m_players[0])
                    card->set_upside_down(true);
            }
            player.hand.append(card);
        }
        quick_sort(player.hand, hearts_card_less);
        auto card_position = player.first_card_position;
        for (auto& card : player.hand) {
            card->set_position(card_position);
            card_position.translate_by(player.card_offset);
        }
    }

    advance_game();
}

void Game::start_animation(NonnullRefPtrVector<Card> cards, Gfx::IntPoint const& end, Function<void()> did_finish_callback, int initial_delay_ms, int steps)
{
    stop_animation();

    m_animation_end = end;
    m_animation_current_step = 0;
    m_animation_steps = steps;
    m_animation_cards.clear_with_capacity();
    for (auto& card : cards)
        m_animation_cards.empend(card, card.position());
    m_animation_did_finish = make<Function<void()>>(move(did_finish_callback));
    m_animation_delay_timer = Core::Timer::create_single_shot(initial_delay_ms, [&] {
        m_animation_playing = true;
        start_timer(10);
    });
    m_animation_delay_timer->start();
}

void Game::stop_animation()
{
    m_animation_playing = false;
    if (m_animation_delay_timer)
        m_animation_delay_timer->stop();
    stop_timer();
}

void Game::timer_event(Core::TimerEvent&)
{
    if (m_animation_playing) {
        for (auto& animation : m_animation_cards) {
            animation.card->set_position(animation.start + (m_animation_end - animation.start) * m_animation_current_step / m_animation_steps);
        }
        if (m_animation_current_step >= m_animation_steps) {
            stop_timer();
            if (m_animation_did_finish)
                (*m_animation_did_finish)();
        }
        m_animation_current_step++;
    }
    update();
}

bool Game::other_player_has_lower_value_card(Player& player, Card& card)
{
    for (auto& other_player : m_players) {
        if (&player != &other_player) {
            for (auto& other_card : other_player.hand) {
                if (other_card && card.type() == other_card->type() && hearts_card_value(*other_card) < hearts_card_value(card))
                    return true;
            }
        }
    }
    return false;
}

bool Game::other_player_has_higher_value_card(Player& player, Card& card)
{
    for (auto& other_player : m_players) {
        if (&player != &other_player) {
            for (auto& other_card : other_player.hand) {
                if (other_card && card.type() == other_card->type() && hearts_card_value(*other_card) > hearts_card_value(card))
                    return true;
            }
        }
    }
    return false;
}

#define RETURN_CARD_IF_VALID(card)     \
    do {                               \
        auto card_index = (card);      \
        if (card_index.has_value())    \
            return card_index.value(); \
    } while (0)

size_t Game::pick_card(Player& player)
{
    bool is_leading_player = m_trick.is_empty();
    bool is_first_trick = m_trick_number == 0;
    if (is_leading_player) {
        if (is_first_trick) {
            auto clubs_2 = player.pick_specific_card(Card::Type::Clubs, CardValue::Number_2);
            VERIFY(clubs_2.has_value());
            return clubs_2.value();
        } else {
            auto valid_card = [this, &player](Card& card) {
                return is_valid_play(player, card);
            };
            auto prefer_card = [this, &player](Card& card) {
                return !other_player_has_lower_value_card(player, card) && other_player_has_higher_value_card(player, card);
            };
            auto lower_value_card_in_play = [this, &player](Card& card) {
                return other_player_has_lower_value_card(player, card);
            };
            return player.pick_lead_card(move(valid_card), move(prefer_card), move(lower_value_card_in_play));
        }
    }
    auto* high_card = &m_trick[0];
    for (auto& card : m_trick)
        if (high_card->type() == card.type() && hearts_card_value(card) > hearts_card_value(*high_card))
            high_card = &card;
    if (high_card->type() == Card::Type::Spades && hearts_card_value(*high_card) > CardValue::Queen)
        RETURN_CARD_IF_VALID(player.pick_specific_card(Card::Type::Spades, CardValue::Queen));
    auto card_has_points = [](Card& card) { return hearts_card_points(card) > 0; };
    auto trick_has_points = m_trick.first_matching(card_has_points).has_value();
    bool is_trailing_player = m_trick.size() == 3;
    if (!trick_has_points && is_trailing_player) {
        RETURN_CARD_IF_VALID(player.pick_low_points_high_value_card(m_trick[0].type()));
        if (is_first_trick)
            return player.pick_low_points_high_value_card().value();
        else
            return player.pick_max_points_card();
    }
    RETURN_CARD_IF_VALID(player.pick_lower_value_card(*high_card));
    if (!is_trailing_player)
        RETURN_CARD_IF_VALID(player.pick_slightly_higher_value_card(*high_card));
    else
        RETURN_CARD_IF_VALID(player.pick_low_points_high_value_card(high_card->type()));
    if (is_first_trick)
        return player.pick_low_points_high_value_card().value();
    else
        return player.pick_max_points_card();
}

void Game::let_player_play_card()
{
    auto& player = current_player();

    if (&player == &m_players[0])
        on_status_change("Select a card to play.");
    else
        on_status_change(String::formatted("Waiting for {} to play a card...", player));

    if (player.is_human) {
        m_human_can_play = true;
        update();
        return;
    }

    play_card(player, pick_card(player));
}

size_t Game::player_index(Player& player)
{
    return &player - m_players;
}

Player& Game::current_player()
{
    VERIFY(m_trick.size() < 4);
    auto player_index = m_leading_player - m_players;
    auto current_player_index = (player_index + m_trick.size()) % 4;
    dbgln_if(HEARTS_DEBUG, "Leading player: {}, current player: {}", *m_leading_player, m_players[current_player_index]);
    return m_players[current_player_index];
}

void Game::continue_game_after_delay(int interval_ms)
{
    m_delay_timer->start(interval_ms);
}

void Game::advance_game()
{
    if (game_ended()) {
        on_status_change("Game ended.");
        return;
    }

    if (m_trick_number == 0 && m_trick.is_empty()) {
        // Find whoever has 2 of Clubs, they get to play the first card
        for (auto& player : m_players) {
            auto clubs_2_card = player.hand.first_matching([](auto& card) {
                return card->type() == Card::Type::Clubs && hearts_card_value(*card) == CardValue::Number_2;
            });
            if (clubs_2_card.has_value()) {
                m_leading_player = &player;
                let_player_play_card();
                return;
            }
        }
    }

    if (m_trick.size() < 4) {
        let_player_play_card();
        return;
    }

    auto leading_card_type = m_trick[0].type();
    size_t taker_index = 0;
    auto taker_value = hearts_card_value(m_trick[0]);
    for (size_t i = 1; i < 4; i++) {
        if (m_trick[i].type() != leading_card_type)
            continue;
        if (hearts_card_value(m_trick[i]) <= taker_value)
            continue;
        taker_index = i;
        taker_value = hearts_card_value(m_trick[i]);
    }
    auto leading_player_index = player_index(*m_leading_player);
    auto taking_player_index = (leading_player_index + taker_index) % 4;
    auto& taking_player = m_players[taking_player_index];
    dbgln_if(HEARTS_DEBUG, "{} takes the trick", taking_player);
    for (auto& card : m_trick) {
        if (hearts_card_points(card) == 0)
            continue;
        dbgln_if(HEARTS_DEBUG, "{} takes card {}", taking_player, card);
        taking_player.cards_taken.append(card);
    }

    start_animation(
        m_trick,
        taking_player.taken_cards_target,
        [&] {
            ++m_trick_number;

            if (game_ended())
                for (auto& player : m_players)
                    quick_sort(player.cards_taken, hearts_card_less);

            m_trick.clear_with_capacity();
            m_leading_player = &taking_player;
            update();
            dbgln_if(HEARTS_DEBUG, "-----");
            advance_game();
        },
        750);

    return;
}

void Game::keydown_event(GUI::KeyEvent& event)
{
    if (event.shift() && event.key() == KeyCode::Key_F10) {
        m_players[0].is_human = !m_players[0].is_human;
        advance_game();
    } else if (event.key() == KeyCode::Key_F10) {
        if (m_human_can_play)
            play_card(m_players[0], pick_card(m_players[0]));
    } else if (event.shift() && event.key() == KeyCode::Key_F11)
        dump_state();
}

void Game::play_card(Player& player, size_t card_index)
{
    if (player.is_human)
        m_human_can_play = false;
    VERIFY(player.hand[card_index]);
    VERIFY(m_trick.size() < 4);
    RefPtr<Card> card;
    swap(player.hand[card_index], card);
    dbgln_if(HEARTS_DEBUG, "{} plays {}", player, *card);
    VERIFY(is_valid_play(player, *card));
    card->set_upside_down(false);
    m_trick.append(*card);

    const Gfx::IntPoint trick_card_positions[] = {
        { width / 2 - Card::width / 2, height / 2 - 30 },
        { width / 2 - Card::width + 15, height / 2 - Card::height / 2 - 15 },
        { width / 2 - Card::width / 2 + 15, height / 2 - Card::height + 15 },
        { width / 2, height / 2 - Card::height / 2 },
    };

    VERIFY(m_leading_player);
    size_t leading_player_index = player_index(*m_leading_player);

    NonnullRefPtrVector<Card> cards;
    cards.append(*card);
    start_animation(
        cards,
        trick_card_positions[(leading_player_index + m_trick.size() - 1) % 4],
        [&] {
            advance_game();
        },
        0);
}

bool Game::is_valid_play(Player& player, Card& card, String* explanation) const
{
    // First card must be 2 of Clubs.
    if (m_trick_number == 0 && m_trick.is_empty()) {
        if (explanation)
            *explanation = "The first card must be Two of Clubs.";
        return card.type() == Card::Type::Clubs && hearts_card_value(card) == CardValue::Number_2;
    }

    // Can't play hearts or The Queen in the first trick.
    if (m_trick_number == 0 && hearts_card_points(card) > 0) {
        bool all_points_cards = true;
        for (auto& other_card : player.hand) {
            if (hearts_card_points(*other_card) == 0) {
                all_points_cards = false;
                break;
            }
        }
        // ... unless the player only has points cards (e.g. all Hearts or
        // 12 Hearts + Queen of Spades), in which case they're allowed to play Hearts.
        if (all_points_cards && card.type() == Card::Type::Hearts)
            return true;
        if (explanation)
            *explanation = "You can't play a card worth points in the first trick.";
        return false;
    }

    // Leading card can't be hearts until hearts are broken
    // unless the player only has hearts cards.
    if (m_trick.is_empty()) {
        if (are_hearts_broken() || card.type() != Card::Type::Hearts)
            return true;
        auto non_hearts_card = player.hand.first_matching([](auto const& other_card) {
            return !other_card.is_null() && other_card->type() != Card::Type::Hearts;
        });
        auto only_has_hearts = !non_hearts_card.has_value();
        if (!only_has_hearts && explanation)
            *explanation = "Hearts haven't been broken.";
        return only_has_hearts;
    }

    // Player must follow suit unless they don't have any matching cards.
    auto leading_card_type = m_trick[0].type();
    if (leading_card_type == card.type())
        return true;
    auto has_matching_card = player.has_card_of_type(leading_card_type);
    if (has_matching_card && explanation)
        *explanation = "You must follow suit.";
    return !has_matching_card;
}

bool Game::are_hearts_broken() const
{
    for (auto& player : m_players)
        for (auto& card : player.cards_taken)
            if (card->type() == Card::Type::Hearts)
                return true;
    return false;
}

void Game::mouseup_event(GUI::MouseEvent& event)
{
    GUI::Frame::mouseup_event(event);

    if (event.button() != GUI::MouseButton::Left)
        return;

    if (!m_human_can_play)
        return;

    for (ssize_t i = m_players[0].hand.size() - 1; i >= 0; i--) {
        auto& card = m_players[0].hand[i];
        if (card.is_null())
            continue;
        if (card->rect().contains(event.position())) {
            String explanation;
            if (!is_valid_play(m_players[0], *card, &explanation)) {
                on_status_change(String::formatted("You can't play this card: {}", explanation));
                continue_game_after_delay();
                return;
            }
            play_card(m_players[0], i);
            update();
            break;
        }
    }
}

bool Game::is_winner(Player& player)
{
    Optional<int> min_score;
    Optional<int> max_score;
    int player_score = 0;
    for (auto& other_player : m_players) {
        int score = 0;
        for (auto& card : other_player.cards_taken)
            if (card->type() == Card::Type::Spades && card->value() == 11)
                score += 13;
            else if (card->type() == Card::Type::Hearts)
                score++;
        if (!min_score.has_value() || score < min_score.value())
            min_score = score;
        if (!max_score.has_value() || score > max_score.value())
            max_score = score;
        if (&other_player == &player)
            player_score = score;
    }
    constexpr int sum_points_of_all_cards = 26;
    return (max_score.value() != sum_points_of_all_cards && player_score == min_score.value()) || player_score == sum_points_of_all_cards;
}

void Game::paint_event(GUI::PaintEvent& event)
{
    GUI::Frame::paint_event(event);

    GUI::Painter painter(*this);
    painter.add_clip_rect(frame_inner_rect());
    painter.add_clip_rect(event.rect());

    static Gfx::Color s_background_color = palette().color(background_role());
    painter.clear_rect(frame_inner_rect(), s_background_color);

    for (auto& player : m_players) {
        auto& font = painter.font().bold_variant();
        auto font_color = game_ended() && is_winner(player) ? Color::Blue : Color::Black;
        painter.draw_text(player.name_position, player.name, font, player.name_alignment, font_color, Gfx::TextElision::None);

        if (!game_ended()) {
            for (auto& card : player.hand)
                if (!card.is_null())
                    card->draw(painter);
        } else {
            // FIXME: reposition cards in advance_game() maybe
            auto card_position = player.first_card_position;
            for (auto& card : player.cards_taken) {
                card->set_upside_down(false);
                card->set_position(card_position);
                card->draw(painter);
                card_position.translate_by(player.card_offset);
            }
        }
    }

    for (size_t i = 0; i < m_trick.size(); i++)
        m_trick[i].draw(painter);
}

void Game::dump_state() const
{
    if constexpr (HEARTS_DEBUG) {
        dbgln("------------------------------");
        for (uint8_t i = 0; i < 4; ++i) {
            auto& player = m_players[i];
            dbgln("Player {}", player.name);
            dbgln("Hand:");
            for (const auto& card : player.hand)
                if (card.is_null())
                    dbgln("  <empty>");
                else
                    dbgln("  {}", *card);
            dbgln("Taken:");
            for (const auto& card : player.cards_taken)
                dbgln("  {}", card);
        }
    }
}

}
