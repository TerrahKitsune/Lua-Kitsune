#pragma once

struct BinaryTreeNode {

	long long key;
	void* data;
	struct BinaryTreeNode* Left;
	struct BinaryTreeNode* Right;
};

int DeleteInTree(BinaryTreeNode** root, long long key);
BinaryTreeNode * AddToTree(BinaryTreeNode * root, long long key, void* data);
BinaryTreeNode * GetInTree(BinaryTreeNode* root, long long key);
void DestroyTree(BinaryTreeNode* root);