#include <cstddef>
#include <string>

#include "btree/btree.h"
#include "btree/btree_test_utils.h"
#include "btree/builder/btree_builder.h"
#include "db_test_fixture.h"
#include "pager/leaf_page/leaf_page.h"
#include "pager/node_page/node_page.h"
#include <gtest/gtest.h>

class BTreeCursorDeleteTest : public DbTestFixture {};

TEST_F(BTreeCursorDeleteTest, BTreeDeleteBasic1) {
  ASSERT_NE(db_file_ptr, nullptr);
  ASSERT_NE(pager, nullptr);

  BTreeConfig config;
  config.node_min_cells = config.leaf_min_cells = 2;
  config.node_max_cells = config.leaf_max_cells = 3;

  PageNumber root_pgno = BTreeBuilder::build_tree(
      "tests/btree/configs/basic1.yaml", pager.get(), config);

  NodePageManager init_npm(root_pgno, db_file_ptr);
  PageNumber node1_pgno = init_npm.cells_[0].left_child;
  PageNumber node2_pgno = init_npm.cells_[1].left_child;
  PageNumber node3_pgno = init_npm.right_child_;

  BTreeCursor cursor(pager.get(), root_pgno, config);

  ASSERT_TRUE(cursor.move_to_key(10));
  cursor.remove();

  NodePageManager npm1(root_pgno, db_file_ptr);
  ASSERT_EQ(npm1.num_cells_, 2);
  ASSERT_EQ(npm1.cells_[0].key, 4);
  ASSERT_EQ(npm1.cells_[1].key, 12);
  ASSERT_EQ(npm1.cells_[0].left_child, node1_pgno);
  ASSERT_EQ(npm1.cells_[1].left_child, node2_pgno);
  ASSERT_EQ(npm1.right_child_, node3_pgno);

  LeafPageManager lpm1a(node1_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm1a.num_cells_, 2);
  ASSERT_EQ(lpm1a.cells_[0].key, 1);
  ASSERT_EQ(lpm1a.cells_[1].key, 3);

  LeafPageManager lpm2a(node2_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm2a.num_cells_, 2);
  ASSERT_EQ(lpm2a.cells_[0].key, 5);
  ASSERT_EQ(lpm2a.cells_[1].key, 9);

  LeafPageManager lpm3a(node3_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm3a.num_cells_, 2);
  ASSERT_EQ(lpm3a.cells_[0].key, 12);
  ASSERT_EQ(lpm3a.cells_[1].key, 13);

  ASSERT_EQ(lpm1a.prev_page(), NULL_PAGE);
  ASSERT_EQ(lpm1a.next_page(), node2_pgno);
  ASSERT_EQ(lpm2a.prev_page(), node1_pgno);
  ASSERT_EQ(lpm2a.next_page(), node3_pgno);
  ASSERT_EQ(lpm3a.prev_page(), node2_pgno);
  ASSERT_EQ(lpm3a.next_page(), NULL_PAGE);

  ASSERT_TRUE(cursor.move_to_key(5));
  cursor.remove();

  ASSERT_EQ(cursor.current_key(), 9);

  NodePageManager npm2(root_pgno, db_file_ptr);
  ASSERT_EQ(npm2.num_cells_, 1);
  ASSERT_EQ(npm2.cells_[0].key, 4);
  ASSERT_EQ(npm2.cells_[0].left_child, node1_pgno);
  ASSERT_EQ(npm2.right_child_, node3_pgno);

  LeafPageManager lpm1b(node1_pgno, db_file_ptr, pager.get());
  LeafPageManager lpm3b(node3_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm3b.num_cells_, 3);
  ASSERT_EQ(lpm3b.cells_[0].key, 9);
  ASSERT_EQ(lpm3b.cells_[1].key, 12);
  ASSERT_EQ(lpm3b.cells_[2].key, 13);

  ASSERT_EQ(lpm1b.prev_page(), NULL_PAGE);
  ASSERT_EQ(lpm1b.next_page(), node3_pgno);
  ASSERT_EQ(lpm3b.prev_page(), node1_pgno);
  ASSERT_EQ(lpm3b.next_page(), NULL_PAGE);

  ASSERT_TRUE(cursor.move_to_key(9));
  cursor.remove();

  ASSERT_EQ(cursor.current_key(), 12);

  NodePageManager npm3(root_pgno, db_file_ptr);
  ASSERT_EQ(npm3.num_cells_, 1);
  ASSERT_EQ(npm3.cells_[0].key, 4);
  ASSERT_EQ(npm3.cells_[0].left_child, node1_pgno);
  ASSERT_EQ(npm3.right_child_, node3_pgno);

  LeafPageManager lpm1c(node1_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm1c.num_cells_, 2);
  ASSERT_EQ(lpm1c.cells_[0].key, 1);
  ASSERT_EQ(lpm1c.cells_[1].key, 3);
  assert_payload(lpm1c.cells_[0].record_page, db_file_ptr, "111");
  assert_payload(lpm1c.cells_[1].record_page, db_file_ptr, "333");

  LeafPageManager lpm3c(node3_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm3c.num_cells_, 2);
  ASSERT_EQ(lpm3c.cells_[0].key, 12);
  ASSERT_EQ(lpm3c.cells_[1].key, 13);
  assert_payload(lpm3c.cells_[0].record_page, db_file_ptr, "121212");
  assert_payload(lpm3c.cells_[1].record_page, db_file_ptr, "131313");

  ASSERT_EQ(lpm1c.prev_page(), NULL_PAGE);
  ASSERT_EQ(lpm1c.next_page(), node3_pgno);
  ASSERT_EQ(lpm3c.prev_page(), node1_pgno);
  ASSERT_EQ(lpm3c.next_page(), NULL_PAGE);

  ASSERT_TRUE(cursor.move_to_key(3));
  cursor.remove();

  ASSERT_EQ(cursor.current_key(), 12);

  PageNumber new_root_pgno = cursor.get_root_pgno();
  ASSERT_EQ(new_root_pgno, node3_pgno);
  ASSERT_EQ(pager->get_page_type(new_root_pgno), PAGER_LEAF_PAGE);
  ASSERT_FALSE(cursor.is_empty());

  LeafPageManager root_lpm(new_root_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(root_lpm.num_cells_, 3);
  ASSERT_EQ(root_lpm.cells_[0].key, 1);
  ASSERT_EQ(root_lpm.cells_[1].key, 12);
  ASSERT_EQ(root_lpm.cells_[2].key, 13);
  assert_payload(root_lpm.cells_[0].record_page, db_file_ptr, "111");
  assert_payload(root_lpm.cells_[1].record_page, db_file_ptr, "121212");
  assert_payload(root_lpm.cells_[2].record_page, db_file_ptr, "131313");
  ASSERT_EQ(root_lpm.prev_page(), NULL_PAGE);
  ASSERT_EQ(root_lpm.next_page(), NULL_PAGE);

  ASSERT_TRUE(cursor.move_to_key(1));
  cursor.remove();
  ASSERT_EQ(cursor.current_key(), 12);

  ASSERT_TRUE(cursor.move_to_key(12));
  cursor.remove();
  ASSERT_EQ(cursor.current_key(), 13);

  ASSERT_TRUE(cursor.move_to_key(13));
  cursor.remove();
  ASSERT_TRUE(cursor.is_empty());
}

TEST_F(BTreeCursorDeleteTest, BTreeDeleteBasic3) {
  ASSERT_NE(db_file_ptr, nullptr);
  ASSERT_NE(pager, nullptr);

  BTreeConfig config;
  config.node_min_cells = config.leaf_min_cells = 2;
  config.node_max_cells = config.leaf_max_cells = 3;

  PageNumber root_pgno = BTreeBuilder::build_tree(
      "tests/btree/configs/basic3.yaml", pager.get(), config);

  BTreeCursor cursor(pager.get(), root_pgno, config);

  ASSERT_TRUE(cursor.move_to_key(7));
  cursor.remove();

  ASSERT_EQ(cursor.current_key(), 9);

  NodePageManager npm(root_pgno, db_file_ptr);
  PageNumber lpm2_pgno = npm.cells_[1].left_child;
  assert_cursor_stack(cursor.get_cursor_stack(), {
                                                     {lpm2_pgno, 1},
                                                     {root_pgno, 1},
                                                 });
  ASSERT_EQ(npm.num_cells_, 2);
  ASSERT_EQ(npm.cells_[0].key, 4);
  ASSERT_EQ(npm.cells_[1].key, 12);

  LeafPageManager lpm1(npm.cells_[0].left_child, db_file_ptr, pager.get());
  ASSERT_EQ(lpm1.num_cells_, 2);
  ASSERT_EQ(lpm1.cells_[0].key, 1);
  ASSERT_EQ(lpm1.cells_[1].key, 3);
  assert_payload(lpm1.cells_[0].record_page, db_file_ptr, "111");
  assert_payload(lpm1.cells_[1].record_page, db_file_ptr, "333");

  LeafPageManager lpm2(npm.cells_[1].left_child, db_file_ptr, pager.get());
  ASSERT_EQ(lpm2.num_cells_, 2);
  ASSERT_EQ(lpm2.cells_[0].key, 5);
  ASSERT_EQ(lpm2.cells_[1].key, 9);
  assert_payload(lpm2.cells_[0].record_page, db_file_ptr, "555");
  assert_payload(lpm2.cells_[1].record_page, db_file_ptr, "999");

  LeafPageManager lpm3(npm.right_child_, db_file_ptr, pager.get());
  ASSERT_EQ(lpm3.num_cells_, 2);
  ASSERT_EQ(lpm3.cells_[0].key, 12);
  ASSERT_EQ(lpm3.cells_[1].key, 14);
  assert_payload(lpm3.cells_[0].record_page, db_file_ptr, "121212");
  assert_payload(lpm3.cells_[1].record_page, db_file_ptr, "141414");

  ASSERT_EQ(lpm1.prev_page(), NULL_PAGE);
  ASSERT_EQ(lpm1.next_page(), lpm2.pgno_);

  ASSERT_EQ(lpm2.prev_page(), lpm1.pgno_);
  ASSERT_EQ(lpm2.next_page(), lpm3.pgno_);

  ASSERT_EQ(lpm3.prev_page(), lpm2.pgno_);
  ASSERT_EQ(lpm3.next_page(), NULL_PAGE);
}

TEST_F(BTreeCursorDeleteTest, BTreeDeleteDelete1) {
  ASSERT_NE(db_file_ptr, nullptr);
  ASSERT_NE(pager, nullptr);

  BTreeConfig config;
  config.node_min_cells = config.leaf_min_cells = 2;
  config.node_max_cells = config.leaf_max_cells = 4;

  PageNumber root_pgno = BTreeBuilder::build_tree(
      "tests/btree/configs/delete1.yaml", pager.get(), config);

  BTreeCursor cursor(pager.get(), root_pgno, config);

  ASSERT_TRUE(cursor.move_to_key(15));
  cursor.remove();

  ASSERT_EQ(cursor.current_key(), 17);

  NodePageManager root_npm(root_pgno, db_file_ptr);
  ASSERT_EQ(root_npm.num_cells_, 1);
  ASSERT_EQ(root_npm.cells_[0].key, 13);

  PageNumber left_node_pgno = root_npm.cells_[0].left_child;
  NodePageManager left_npm(left_node_pgno, db_file_ptr);
  PageNumber leaf3_pgno = left_npm.right_child_;

  PageNumber right_node_pgno = root_npm.right_child_;
  NodePageManager right_npm(right_node_pgno, db_file_ptr);

  ASSERT_EQ(right_npm.num_cells_, 2);
  ASSERT_EQ(right_npm.cells_[0].key, 19);
  ASSERT_EQ(right_npm.cells_[1].key, 21);

  PageNumber leaf4_pgno = right_npm.cells_[0].left_child;
  PageNumber leaf5_pgno = right_npm.cells_[1].left_child;
  PageNumber leaf6_pgno = right_npm.right_child_;

  assert_cursor_stack(cursor.get_cursor_stack(), {
                                                     {leaf4_pgno, 1},
                                                     {right_node_pgno, 0},
                                                     {root_pgno, 1},
                                                 });

  LeafPageManager lpm4(leaf4_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm4.num_cells_, 2);
  ASSERT_EQ(lpm4.cells_[0].key, 13);
  ASSERT_EQ(lpm4.cells_[1].key, 17);
  assert_payload(lpm4.cells_[0].record_page, db_file_ptr, "13");
  assert_payload(lpm4.cells_[1].record_page, db_file_ptr, "17");

  LeafPageManager lpm5(leaf5_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm5.num_cells_, 2);
  ASSERT_EQ(lpm5.cells_[0].key, 19);
  ASSERT_EQ(lpm5.cells_[1].key, 20);
  assert_payload(lpm5.cells_[0].record_page, db_file_ptr, "19");
  assert_payload(lpm5.cells_[1].record_page, db_file_ptr, "20");

  LeafPageManager lpm6(leaf6_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(lpm6.num_cells_, 2);
  ASSERT_EQ(lpm6.cells_[0].key, 21);
  ASSERT_EQ(lpm6.cells_[1].key, 23);
  assert_payload(lpm6.cells_[0].record_page, db_file_ptr, "21");
  assert_payload(lpm6.cells_[1].record_page, db_file_ptr, "23");

  ASSERT_EQ(lpm4.prev_page(), leaf3_pgno);
  ASSERT_EQ(lpm4.next_page(), lpm5.pgno_);

  ASSERT_EQ(lpm5.prev_page(), lpm4.pgno_);
  ASSERT_EQ(lpm5.next_page(), lpm6.pgno_);

  ASSERT_EQ(lpm6.prev_page(), lpm5.pgno_);
  ASSERT_EQ(lpm6.next_page(), NULL_PAGE);

  ASSERT_TRUE(cursor.move_to_key(19));
  cursor.remove();

  ASSERT_EQ(cursor.current_key(), 20);

  PageNumber new_root_pgno = cursor.get_root_pgno();
  NodePageManager new_root_npm(new_root_pgno, db_file_ptr);
  ASSERT_EQ(new_root_npm.num_cells_, 4);
  ASSERT_EQ(new_root_npm.cells_[0].key, 5);
  ASSERT_EQ(new_root_npm.cells_[1].key, 9);
  ASSERT_EQ(new_root_npm.cells_[2].key, 13);
  ASSERT_EQ(new_root_npm.cells_[3].key, 19);

  PageNumber new_leaf1_pgno = new_root_npm.cells_[0].left_child;
  PageNumber new_leaf2_pgno = new_root_npm.cells_[1].left_child;
  PageNumber new_leaf3_pgno = new_root_npm.cells_[2].left_child;
  PageNumber new_leaf4_pgno = new_root_npm.cells_[3].left_child;
  PageNumber new_leaf6_pgno = new_root_npm.right_child_;

  assert_cursor_stack(cursor.get_cursor_stack(), {
                                                     {new_leaf6_pgno, 0},
                                                     {new_root_pgno, 4},
                                                 });

  LeafPageManager new_lpm1(new_leaf1_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(new_lpm1.num_cells_, 2);
  ASSERT_EQ(new_lpm1.cells_[0].key, 1);
  ASSERT_EQ(new_lpm1.cells_[1].key, 3);

  LeafPageManager new_lpm2(new_leaf2_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(new_lpm2.num_cells_, 2);
  ASSERT_EQ(new_lpm2.cells_[0].key, 5);
  ASSERT_EQ(new_lpm2.cells_[1].key, 7);

  LeafPageManager new_lpm3(new_leaf3_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(new_lpm3.num_cells_, 2);
  ASSERT_EQ(new_lpm3.cells_[0].key, 9);
  ASSERT_EQ(new_lpm3.cells_[1].key, 11);

  LeafPageManager new_lpm4(new_leaf4_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(new_lpm4.num_cells_, 2);
  ASSERT_EQ(new_lpm4.cells_[0].key, 13);
  ASSERT_EQ(new_lpm4.cells_[1].key, 17);

  LeafPageManager new_lpm6(new_leaf6_pgno, db_file_ptr, pager.get());
  ASSERT_EQ(new_lpm6.num_cells_, 3);
  ASSERT_EQ(new_lpm6.cells_[0].key, 20);
  ASSERT_EQ(new_lpm6.cells_[1].key, 21);
  ASSERT_EQ(new_lpm6.cells_[2].key, 23);
  assert_payload(new_lpm6.cells_[0].record_page, db_file_ptr, "20");
  assert_payload(new_lpm6.cells_[1].record_page, db_file_ptr, "21");
  assert_payload(new_lpm6.cells_[2].record_page, db_file_ptr, "23");

  ASSERT_EQ(new_lpm4.next_page(), new_leaf6_pgno);
  ASSERT_EQ(new_lpm6.prev_page(), new_leaf4_pgno);
  ASSERT_EQ(new_lpm6.next_page(), NULL_PAGE);
}
